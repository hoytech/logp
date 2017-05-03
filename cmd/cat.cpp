#include <unistd.h>
#include <string.h>
#include <time.h>

#include <iostream>
#include <string>

#include "nlohmann/json.hpp"
#include "hoytech/protected_queue.h"

#include "logp/cmd/cat.h"
#include "logp/websocket.h"
#include "logp/util.h"


namespace logp { namespace cmd {


uint64_t opt_event_id = 0;


const char *cat::usage() {
    static const char *u =
        "logp cat [options]\n"
        "  -e/--event [event id]   Event to cat"
    ;

    return u;
}

const char *cat::getopt_string() { return "e:"; }

struct option *cat::get_long_options() {
    static struct option opts[] = {
        {"event", required_argument, 0, 'e'},
        {0, 0, 0, 0}
    };

    return opts;
}

void cat::process_option(int arg, int, char *) {
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

    if (j["ty"] == "stdout") {
        std::cout << txt;
    } else if (j["ty"] == "stderr") {
        std::cerr << txt;
    }
}



struct cat_msg_end_seen {};

struct cat_msg_monitoring {};

struct cat_msg_entry {
    nlohmann::json entry;
};

using cat_msg = mapbox::util::variant<cat_msg_end_seen, cat_msg_monitoring, cat_msg_entry>;



void cat::execute() {
    if (!opt_event_id) {
        PRINT_ERROR << "Must provide an event id with -e";
        print_usage_and_exit();
    }


    hoytech::protected_queue<cat_msg> cmd_cat_queue;
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
                cmd_cat_queue.push_move(cat_msg_end_seen{});
            } else {
                cmd_cat_queue.push_move(cat_msg_entry{std::move(res)});
            }
        };
        r.on_monitoring = [&]{
            cmd_cat_queue.push_move(cat_msg_monitoring{});
        };

        ws_worker.push_move_new_request(r);
    }

    while (1) {
        auto mv = cmd_cat_queue.shift();

        mv.match(
            [&](cat_msg_end_seen &){
                end_seen = true;

                if (monitoring) exit(0);
            },
            [&](cat_msg_monitoring &){
                monitoring = true;

                std::sort(entries.begin(), entries.end(), [](const nlohmann::json &a, const nlohmann::json &b){
                    return a.at("at") < b.at("at");
                });

                for (auto &e : entries) {
                    do_output(e);
                }

                if (end_seen) exit(0);
            },
            [&](cat_msg_entry &e){
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
