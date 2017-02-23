#include <unistd.h>
#include <string.h>
#include <time.h>

#include <iostream>
#include <string>
#include <exception>

#include "nlohmann/json.hpp"

#include "logp/cmd/ps.h"
#include "logp/websocket.h"
#include "logp/print.h"


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





static bool finished_history = false;



static std::string render_in_progress_header() {
    return "EVID\tSTART\tUSER@HOSTNAME\t\tPID\tCMD";
}


static std::string render_command(nlohmann::json &res) {
    std::string output;

    if (!res.count("da") || !res["da"].count("cmd")) return "?";

    auto &cmd = res["da"]["cmd"];

    for (size_t i=0; i<cmd.size(); i++) {
        std::string v = cmd[i];

        if (v.find_first_of(" \t\r\n$<>{}'\"") != std::string::npos) {
            size_t pos = 0;
            std::string search = "'";
            std::string replace = "'\\''";
            while ((pos = v.find(search, pos)) != std::string::npos) {
                 v.replace(pos, search.length(), replace);
                 pos += replace.length();
            }

            v = std::string("'") + v + std::string("'");
        }

        output += v;
        if (i != cmd.size()-1) output += " ";
    }

    return output;
}


static std::string render_userhost(nlohmann::json &res) {
    std::string output;

    if (!res.count("da")) return "?";
    auto &data = res["da"];

    output += data.count("user") ? data["user"] : "?";
    output += "@";
    output += data.count("hostname") ? data["hostname"] : "?";

    return output;
}


static std::string render_duration(uint64_t start, uint64_t end=0) {
    start /= 1000000;

    if (end == 0) end = ::time(nullptr);
    else end /= 1000000;

    uint64_t delta = end - start;

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


static std::string render_time(uint64_t time_us) {
    time_t time_s = time_us / 1000000;

    struct tm timeres;
    if (!localtime_r(&time_s, &timeres)) throw std::runtime_error(std::string("error parsing time"));

    char buf[100];
    strftime(buf, sizeof(buf), "%b%d %H:%M:%S", &timeres);

    return std::string(buf);
}




static std::string render_in_progress(nlohmann::json &res) {
    std::string output;

    bool has_data = !!res.count("da");
    auto &data = res["da"];

    output += res.count("ev") ? std::to_string(static_cast<uint64_t>(res["ev"])) : "?";
    output += "\t";

    output += res.count("st") ? render_duration(res["st"]) : "?";
    output += "\t";

    output += render_userhost(res);
    output += "\t\t";

    output += has_data && data.count("pid") ? std::to_string(static_cast<uint64_t>(data["pid"])) : "?";
    output += "\t";

    output += render_command(res);
    output += "\t";

    return output;
}



class event_rec {
  public:
    event_rec(uint64_t start_, uint64_t lane_, std::string &evid_, std::string &pid_) : start(start_), lane(lane_), evid(evid_), pid(pid_) {}

    uint64_t start;
    uint64_t lane;
    std::string evid;
    std::string pid;
};



void print_lane_chart(std::vector<bool> &lanes, std::string override, uint64_t override_index) {
    for (uint64_t i=0; i<lanes.size(); i++) {
        if (override.size() && i == override_index) std::cout << override;
        else std::cout << (lanes[i] ? "│" : " ");
    }
}



void process_follow(nlohmann::json &res) {
    static std::unordered_map<uint64_t, event_rec> evid_to_rec;
    static std::vector<bool> lanes;

    if (res.count("st")) {
        uint64_t lane = 0;
        while(1) {
            if (lane >= lanes.size()) {
                lanes.push_back(true);
                break;
            }

            if (!lanes[lane]) {
                lanes[lane] = true;
                break;
            }

            lane++;
        }

        std::string evid = res.count("ev") ? std::to_string(static_cast<uint64_t>(res["ev"])) : "?";
        std::string pid = res.count("da") && res["da"].count("pid") ? std::to_string(static_cast<uint64_t>(res["da"]["pid"])) : "?";

        evid_to_rec.emplace(std::piecewise_construct,
                            std::forward_as_tuple(res["ev"]),
                            std::forward_as_tuple(res["st"], lane, evid, pid));

        std::cout << "[" << render_time(res["st"]) << "] ";
        print_lane_chart(lanes, finished_history ? "┬" : "│", lane);
        std::cout << "\t[+] " << render_command(res) << " (" << render_userhost(res) << " evid=" << evid << " pid=" << pid << ")";
        std::cout << std::endl;
    } else if (res.count("en")) {
        auto find_res = evid_to_rec.find(res["ev"]);

        if (find_res == evid_to_rec.end()) {
            PRINT_WARNING << "saw end event without start, ignoring";
            return;
        }

        auto &rec = find_res->second;

        std::string exit_reason;
        if (res["da"].count("exit")) {
            uint64_t exit_code = res["da"]["exit"];
            if (exit_code) exit_reason = std::string("exit code ") + std::to_string(exit_code);
            else exit_reason = "normal exit";
        } else if (res["da"].count("signal")) {
            std::string sig = res["da"]["signal"];
            exit_reason = std::string("killed by ") + sig;
        } else {
            exit_reason = "?";
        }

        std::cout << "[" << render_time(res["en"]) << "] ";
        print_lane_chart(lanes, "┴", rec.lane);
        std::cout << "\t[-]   " << exit_reason << " (took " << render_duration(rec.start, res["en"]) << ")";
        std::cout << std::endl;

        lanes[rec.lane] = false;
        evid_to_rec.erase(static_cast<uint64_t>(res["ev"]));
    } else {
        print_lane_chart(lanes, "", 0);
        std::cout << std::endl;
    }
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
            } else {
                process_follow(res);
            }
        };
        r.on_finished_history = [&]{
           if (!follow) exit(0);
           finished_history = true;
        };

        ws_worker.push_move_new_request(r);
    }

    pause();
}

}}
