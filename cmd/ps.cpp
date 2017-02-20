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
        //if (strcmp(long_options[option_index].name, "stderr") == 0) {
        //    capture_stderr = true;
        //}
        break;
    };
}

void ps::execute() {
    logp::websocket::worker ws_worker;

    {
        nlohmann::json body;
        body["from"] = 0;

        std::string op("get");
        std::string msg_str = body.dump();

        ws_worker.send_message_move(op, msg_str, [&](std::string &resp) {
            std::cout << "GOT: " << resp << std::endl;
        });
    } 

    pause();
}

}}
