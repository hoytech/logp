#include <unistd.h>
#include <getopt.h>
#include <string.h>

#include <iostream>
#include <string>

#include "logp/messages.h"
#include "logp/websocket.h"
#include "logp/config.h"
#include "logp/util.h"

#include "logp/cmd/run.h"


logp::config conf;



void usage() {
    std::cerr << "Usage: logp ..." << std::endl;
    exit(1);
}


int main(int argc, char **argv) {
    // Argument parsing

    int arg, option_index;

    std::string opt_config;
    std::string opt_url;
    std::string opt_token;

    struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"version", no_argument, 0, 'V'},
        {"config", required_argument, 0, 'c'},
        {"url", required_argument, 0, 0},
        {"token", required_argument, 0, 0},
        {0, 0, 0, 0}
    };

    optind = 1;
    while ((arg = getopt_long_only(argc, argv, "+c:", long_options, &option_index)) != -1) {
        switch (arg) {
          case '?':
          case 'h':
            usage();

          case 0:
            if (strcmp(long_options[option_index].name, "url") == 0) {
                opt_url = std::string(optarg);
            } else if (strcmp(long_options[option_index].name, "token") == 0) {
                opt_token = std::string(optarg);
            }
            break;

          case 'c':
            opt_config = std::string(optarg);
            break;

          default:
            exit(1);
        };
    }


    // Config file loading

    bool config_loaded = false;

    if (opt_config.size()) {
        config_loaded = logp::load_config_file(opt_config, conf);

        if (!config_loaded) {
            std::cerr << "Provided config file '" << opt_config << "' doesn't exist" << std::endl;
            exit(1);
        }
    }

    if (!config_loaded) {
        std::string file = logp::util::get_home_dir();

        if (file.size()) {
            file += "/.logp";
            config_loaded = logp::load_config_file(file, conf);
        } else {
            // unable to determine home directory
        }
    }

    if (!config_loaded) config_loaded = logp::load_config_file("/etc/logp.conf", conf);


    // Override config values with command line args

    if (opt_url.size()) conf.url = opt_url;
    if (opt_token.size()) conf.token = opt_token;


    // Verify necessary settings

    if (!conf.url.size()) {
        std::cerr << "No 'url' option specified" << std::endl;
        usage();
    }

    if (!conf.token.size()) {
        std::cerr << "No 'token' option specified" << std::endl;
        usage();
    }

    if (optind >= argc) {
        std::cerr << "Expected a command, ie 'logp run ...'" << std::endl;
        usage();
    }

    std::string command(argv[optind]);

    if (command == "run") {
        logp::cmd::run(argc-optind-1, argv+optind+1);
    } else {
        std::cerr << "Unknown command: " << command << std::endl;
        usage();
    }



/*
    //std::string uri("ws://localhost:8001");
    std::string uri("ws://localhost:8000/ws/");

    logp::websocket::worker c(uri);
    c.run();

    {
        nlohmann::json j = {{ "id", 123 }, { "op", "get" }};
        std::string data = j.dump();
        data += "\n{}";
        logp::msg::websocket_input m(std::move(data));
        c.input_queue.push_move(m);
        c.trigger_input_queue();
    }

    sleep(100);
*/

    return 0;
}
