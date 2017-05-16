#include <unistd.h>
#include <string.h>
#include <time.h>

#include <iostream>
#include <string>

#include "nlohmann/json.hpp"
#include "hoytech/protected_queue.h"

#include "logp/cmd/tail.h"
#include "logp/websocket.h"
#include "logp/util.h"


namespace logp { namespace cmd {


uint64_t opt_event_id = 0;


const char *tail::usage() {
    static const char *u =
        "logp tail [options]\n"
        "  -e/--event [event id]   Event to tail"
    ;

    return u;
}

const char *tail::getopt_string() { return "e:"; }

struct option *tail::get_long_options() {
    static struct option opts[] = {
        {"event", required_argument, 0, 'e'},
        {0, 0, 0, 0}
    };

    return opts;
}

void tail::process_option(int arg, int, char *) {
    switch (arg) {
      case 0:
        break;

      case 'e':
        opt_event_id = std::stoull(std::string(optarg));
        break;
    };
}




void do_output(nlohmann::json &j) {
    std::string txt = j["da"]["txt"];
    txt = util::utf8_decode_binary(txt);

    if (j["ty"] == "stdout") {
        std::cout << txt;
    } else if (j["ty"] == "stderr") {
        std::cerr << txt;
    }
}



struct tail_msg_end_seen {};

struct tail_msg_monitoring {};

struct tail_msg_entry {
    nlohmann::json entry;
};

using tail_msg = mapbox::util::variant<tail_msg_end_seen, tail_msg_monitoring, tail_msg_entry>;



void tail::execute() {
    if (!opt_event_id) {
        PRINT_ERROR << "Must provide an event id with -e";
        print_usage_and_exit();
    }


    hoytech::protected_queue<tail_msg> cmd_tail_queue;
    std::vector<nlohmann::json> entries;
    bool end_seen = false, monitoring = false;

    logp::websocket::worker ws_worker;

    ws_worker.run();

    {
        nlohmann::json query = {
            { "select", "entry" },
            { "from", nlohmann::json::array({ "ev", opt_event_id }) },
        };

        query["where"]["or"].push_back(nlohmann::json::array({ "ty", "stdout" }));
        query["where"]["or"].push_back(nlohmann::json::array({ "ty", "stderr" }));
        query["where"]["or"].push_back(nlohmann::json::array({ "en" }));

        logp::websocket::request_get r;

        r.query = query;

        r.on_entry = [&](nlohmann::json &res) {
            if (res.count("en")) {
                cmd_tail_queue.push_move(tail_msg_end_seen{});
            } else {
                cmd_tail_queue.push_move(tail_msg_entry{std::move(res)});
            }
        };
        r.on_monitoring = [&]{
            cmd_tail_queue.push_move(tail_msg_monitoring{});
        };

        ws_worker.push_move_new_request(r);
    }

    while (1) {
        auto mv = cmd_tail_queue.shift();

        mv.match(
            [&](tail_msg_end_seen &){
                end_seen = true;

                if (monitoring) exit(0);
            },
            [&](tail_msg_monitoring &){
                monitoring = true;

                std::sort(entries.begin(), entries.end(), [](const nlohmann::json &a, const nlohmann::json &b){
                    return a.at("at") < b.at("at");
                });

                for (auto &e : entries) {
                    do_output(e);
                }

                if (end_seen) exit(0);
            },
            [&](tail_msg_entry &e){
                if (monitoring) {
                    do_output(e.entry);
                } else {
                    entries.emplace_back(std::move(e.entry));
                }
            }
        );
    }
}

}}
