#include <unistd.h>
#include <string.h>
#include <time.h>

#include <iostream>
#include <string>

#include "nlohmann/json.hpp"

#include "logp/cmd/cat.h"
#include "logp/websocket.h"
#include "logp/util.h"


namespace logp { namespace cmd {


const char *cat::usage() {
    static const char *u =
        "logp cat [options]\n"
        "  -f/--follow   Print new data as it appears\n"
    ;

    return u;
}

const char *cat::getopt_string() { return "f"; }

struct option *cat::get_long_options() {
    static struct option opts[] = {
        {0, 0, 0, 0}
    };

    return opts;
}

void cat::process_option(int arg, int, char *) {
    switch (arg) {
      case 0:
        break;
    };
}




void cat::execute() {
    logp::websocket::worker ws_worker;

    ws_worker.run();

    {
        nlohmann::json query = {
            { "select", "entry" },
            { "from", nlohmann::json::array({ "ev", 207 }) },
            { "where", nlohmann::json({}) }
        };

        query["where"]["and"] = nlohmann::json::array({ nlohmann::json::array({ "ty", "stdout" }) });

        logp::websocket::request_get r;

        r.query = query;

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
