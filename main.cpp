#include <unistd.h>
#include <getopt.h>
#include <string.h>

#include <iostream>
#include <string>

#include "logp/messages.h"
#include "logp/websocket.h"
#include "logp/config.h"
#include "logp/util.h"
#include "logp/print.h"

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

    struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"version", no_argument, 0, 'V'},
        {"config", required_argument, 0, 'c'},
        {0, 0, 0, 0}
    };

    optind = 1;
    while ((arg = getopt_long_only(argc, argv, "+c:", long_options, &option_index)) != -1) {
        switch (arg) {
          case '?':
          case 'h':
            usage();

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
            PRINT_ERROR << "Provided config file '" << opt_config << "' doesn't exist";
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


    // Verify necessary settings

    if (!conf.endpoint.size()) {
        PRINT_ERROR << "No 'endpoint' option specified";
        usage();
    }

    if (!conf.apikey.size()) {
        PRINT_ERROR << "No 'apikey' option specified";
        usage();
    }

    if (conf.apikey.size() < 3 || conf.apikey.find('-') == std::string::npos) {
        PRINT_ERROR << "apikey has incorrect format";
        usage();
    }

    if (optind >= argc) {
        PRINT_ERROR << "Expected a command, ie 'logp run ...'";
        usage();
    }


    // Execute command

    std::string command(argv[optind]);

    if (command == "run") {
        logp::cmd::run(argc-optind, argv+optind);
    } else {
        PRINT_ERROR << "Unknown command: " << command;
        usage();
    }


    return 0;
}
