#include <unistd.h>
#include <string.h>

#include <iostream>
#include <string>
#include <exception>

#include "nlohmann/json.hpp"

#include "logp/cmd/ping.h"
#include "logp/websocket.h"
#include "logp/print.h"
#include "logp/util.h"


namespace logp { namespace cmd {


const char *ping::usage() {
    static const char *u =
        "logp ping [options]\n"
        "  -c/--count   Number of pings to send\n"
    ;

    return u;
}

const char *ping::getopt_string() { return "c:"; }

struct option *ping::get_long_options() {
    static struct option opts[] = {
        {"count", required_argument, 0, 'c'},
        {0, 0, 0, 0}
    };

    return opts;
}

void ping::process_option(int arg, int, char *optarg) {
    switch (arg) {
      case 'c':
        {
            long c = atol(optarg);
            if (c <= 0) throw std::runtime_error("bad value for count");
            count = c;
        }
        break;

      case 0:
        break;
    };
}



void ping::execute() {
    logp::websocket::worker ws_worker;

    std::cout << "Connecting to:\n";
    std::cout << "  URI: " << ws_worker.uri << "\n";
    std::cout << "  Key: " << ws_worker.token.substr(0, 4) << "******************\n";
    if (ws_worker.uri.substr(0, 4) == "wss:") {
        if (ws_worker.tls_no_verify) std::cout << "  TLS Verification OFF!\n"; // FIXME: colour red
    } else if (ws_worker.uri.substr(0, 3) == "ws:") {
        std::cout << "  Plain-text connection (not wss://)!\n"; // FIXME: colour red
    }
    std::cout << std::endl;

    uint64_t pings_left = count;

    std::function<void()> send_ping = [&]() {
        {
            logp::websocket::request r;

            uint64_t start = logp::util::curr_time();

            r.op = "png";
            r.on_data = [&, start](nlohmann::json &res){
                uint64_t end = logp::util::curr_time();

                uint64_t server_time = res["time"];
                std::cout << "PONG " << (end-start) << "us " << server_time << std::endl;

                if (pings_left == 1) exit(0);
                else if (pings_left > 1) pings_left--;

                sleep(1);
                send_ping();
            };

            ws_worker.push_move_new_request(r);
        }
    };

    ws_worker.on_ini_response = [&](nlohmann::json &r){
        if (r["ini"] == "ok") {
            uint64_t prot = r["prot"];
            uint64_t time = r["time"];

            std::cout << "Server info:\n";
            std::cout << "  Protocol: " << prot << "\n";
            std::cout << "  Time:     " << time << "\n";
            std::cout << std::endl;

            uint64_t perm = r["perm"];

            std::cout << "Access:\n";
            if (perm == 0) {
                std::cout << "  Permissions: NONE (exiting)\n"; // FIXME red
                exit(0);
            }
            std::string perm_str;
            if (perm & 1) perm_str += " READ";
            if (perm & 2) perm_str += " WRITE";
            std::cout << "  Permissions:" << perm_str << "\n";
            std::cout << std::endl;

            send_ping();
        } else {
            PRINT_ERROR << "ini request failed";
            exit(1);
        }
    };

    ws_worker.run();

/*
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
*/

    pause();
}

}}
