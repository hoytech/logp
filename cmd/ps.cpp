#include <unistd.h>
#include <string.h>
#include <time.h>

#include <iostream>
#include <string>

#include "nlohmann/json.hpp"

#include "logp/cmd/ps.h"
#include "logp/websocket.h"


namespace logp { namespace cmd {


const char *ps::usage() {
    static const char *u =
        "logp ps [options]\n"
        "  -f/--follow   Print new events as they appear\n"
    ;

    return u;
}

const char *ps::getopt_string() { return "f"; }

struct option *ps::get_long_options() {
    static struct option opts[] = {
        {"follow", no_argument, 0, 'f'},
        {0, 0, 0, 0}
    };

    return opts;
}

void ps::process_option(int arg, int) {
    switch (arg) {
      case 'f':
        follow = true;
        break;

      case 0:
        break;
    };
}



static std::string render_in_progress_header() {
    return "EVID\tSTART\tUSER@HOSTNAME\t\tPID\tCMD";
}


static std::string render_command(nlohmann::json &cmd) {
    std::string output;

    for (auto &s : cmd) {
        output += s;
        output += " ";
    }

    return output;
}


static std::string render_time(uint64_t t) {
    time_t now = ::time(nullptr);
    t /= 1000000;

    uint64_t delta = now - t;

    if (delta == 0) return std::string("<1s");
    if (delta < 60) return std::to_string(delta) + "s";

    uint64_t seconds = delta % 60;
    delta /= 60;

    if (delta < 60) return std::to_string(delta) + "m" + std::to_string(seconds) + "s";

    uint64_t minutes = delta % 60;
    delta /= 60;

    if (delta < 24) return std::to_string(delta) + "h" + std::to_string(minutes) + "m";

    uint64_t hours = delta % 24;
    delta /= 24;

    return std::to_string(delta) + "d" + std::to_string(hours) + "h";
}


static std::string render_in_progress(nlohmann::json &res) {
    std::string output;

    bool has_data = !!res.count("da");
    auto &data = res["da"];

    output += res.count("ev") ? std::to_string(static_cast<uint64_t>(res["ev"])) : "?";
    output += "\t";

    output += res.count("st") ? render_time(res["st"]) : "?";
    output += "\t";

    output += has_data && data.count("user") ? data["user"] : "?";
    output += "@";
    output += has_data && data.count("hostname") ? data["hostname"] : "?";
    output += "\t\t";

    output += has_data && data.count("pid") ? std::to_string(static_cast<uint64_t>(data["pid"])) : "?";
    output += "\t";

    output += has_data && data.count("cmd") ? render_command(data["cmd"]) : "?";
    output += "\t";

    return output;
}



void ps::execute() {
    logp::websocket::worker ws_worker;

    if (!follow) {
        std::cout << render_in_progress_header() << std::endl;
    }

    {
        logp::websocket::request r;

        r.op = "get";
        r.body = {{"from", 0}};
        r.on_data = [&](nlohmann::json &res) {
            if (!follow) {
                std::cout << render_in_progress(res) << std::endl;
            }
        };
        r.on_finished_history = [&]{
           if (!follow) exit(0);
        };

        ws_worker.push_move_new_request(r);
    }

    pause();
}

}}
