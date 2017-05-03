#include <unistd.h>
#include <string.h>
#include <time.h>

#include <iostream>
#include <string>

#include "nlohmann/json.hpp"

#include "logp/cmd/get.h"
#include "logp/websocket.h"
#include "logp/util.h"


namespace logp { namespace cmd {


const char *get::usage() {
    static const char *u =
        "logp get [options]\n"
        "  -f/--follow   Print new events as they appear\n"
    ;

    return u;
}

const char *get::getopt_string() { return "f"; }

struct option *get::get_long_options() {
    static struct option opts[] = {
        {0, 0, 0, 0}
    };

    return opts;
}

void get::process_option(int arg, int, char *) {
    switch (arg) {
      case 0:
        break;
    };
}




void get::execute() {
    nlohmann::json query;
    nlohmann::json state;

    if (my_argv[optind]) {
        query = nlohmann::json::parse(my_argv[optind]);

        if (my_argv[optind+1]) {
            state = nlohmann::json::parse(my_argv[optind+1]);
        }
    } else {
        query = nlohmann::json({ { "select", "entry" }, { "from", nlohmann::json::array({ "ev", 0, nullptr }) } });
    }

    logp::websocket::worker ws_worker;

    ws_worker.run();

    {
        logp::websocket::request_get r;

        r.query = query;
        if (state.size()) r.state = state;

        r.on_entry = [&](nlohmann::json &res) {
            std::cout << res.dump() << std::endl;
        };
        r.on_monitoring = [&]{
           std::cout << "MONITORING" << std::endl;
        };

        ws_worker.push_move_new_request(r);
    }

    logp::util::sleep_forever();
}

}}
