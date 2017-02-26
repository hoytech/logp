#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

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



std::string format_ms(int64_t time_us) {
    double d = time_us / 1000.0;
    char buf[100];

    snprintf(buf, sizeof(buf), "%.1f", d);

    return std::string(buf);
}


std::string format_time(uint64_t time_us) {
    time_t secs = time_us / 1000000;
    uint64_t usecs = time_us - ((uint64_t)secs * 1000000);
    char buf[100];
    struct tm tm;

    localtime_r(&secs, &tm);

    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S.", &tm);

    std::string output(buf);

    snprintf(buf, sizeof(buf), "%06g ", (double)usecs);
    output += buf;

    strftime(buf, sizeof(buf), "%Z", &tm);
    output += buf;

    return output;
}


std::string format_ping_delta(uint64_t local_start, uint64_t local_end, uint64_t remote) {
    uint64_t d = local_end - local_start;
    d /= 2;
    d += local_start;
    int64_t delta = (int64_t)remote - d;
    std::string output = format_ms(delta) + "ms";
    if (std::abs(delta) >= 1000000) return logp::util::colour_red(output);
    return output;
}


void ping::execute() {
    logp::websocket::worker ws_worker;

    std::cout << "Connecting to:\n";
    std::cout << "  URI: " << ws_worker.uri << "\n";
    if (ws_worker.uri.substr(0, 4) == "wss:") {
        if (ws_worker.tls_no_verify) std::cout << logp::util::colour_red("  TLS Verification OFF!\n");
    } else if (ws_worker.uri.substr(0, 3) == "ws:") {
        std::cout << logp::util::colour_red("  Plain-text connection (not wss://)!\n");
    }
    std::cout << "  Key: " << ws_worker.token.substr(0, 4) << "******************\n";
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
                std::cout << "PONG rtt=" << format_ms(end-start) << "ms clock_diff=" << format_ping_delta(start, end, server_time) << std::endl;
                if (pings_left == 1) exit(0);
                else if (pings_left > 1) pings_left--;

                logp::util::sleep_seconds(1);
                send_ping();
            };

            ws_worker.push_move_new_request(r);
        }
    };

    uint64_t ini_start = logp::util::curr_time();

    ws_worker.on_ini_response = [&](nlohmann::json &r){
        uint64_t ini_end = logp::util::curr_time();

        if (r["ini"] == "ok") {
            uint64_t prot = r["prot"];
            uint64_t time = r["time"];

            std::cout << "Connection info:\n";
            std::cout << "  Established in: " << format_ms(ini_end-ini_start) << "ms\n";
            std::cout << "  Logp Protocol:  " << prot << "\n";
            std::cout << "  Remote clock:   " << format_time(time) << "\n";
            std::cout << std::endl;

            uint64_t perm = r["perm"];

            std::cout << "Access permissions:\n";
            if (perm == 0) {
                std::cout << logp::util::colour_red("  Permissions: NONE (exiting)\n");
                exit(1);
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

    int retries = 3;

    ws_worker.on_disconnect = [&](std::string reason){
        std::string msg = "Connection failure: ";
        msg += reason;

        retries--;

        if (retries > 0) {
            msg += " [retrying ";
            msg += std::to_string(retries);
            msg += " more times]";
        } else {
            msg += " [aborting]";
        }

        PRINT_ERROR << msg;

        if (retries <= 0) exit(1);
    };

    ws_worker.run();

    logp::util::sleep_forever();
}

}}
