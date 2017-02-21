#include <unistd.h>
#include <string.h>

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

void ps::process_option(int arg) {
    switch (arg) {
      case 'f':
        follow = true;
        break;

      case 0:
        break;
    };
}

void ps::execute() {
    logp::websocket::worker ws_worker;

    {
        logp::websocket::request r;

        r.op = "get";
        r.body = {{"from", 0}};
        r.on_data = [&](nlohmann::json &resp) {
            std::cout << "GOT: " << resp.dump() << std::endl;
        };
        r.on_finished_history = [&]{
           if (!follow) exit(0);
        };

        ws_worker.push_move_new_request(r);
    }

    pause();
}

}}
