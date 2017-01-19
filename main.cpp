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


    // Verify necessary settings

    if (!conf.endpoint.size()) {
        std::cerr << "No 'endpoint' option specified" << std::endl;
        usage();
    }

    if (!conf.apikey.size()) {
        std::cerr << "No 'apikey' option specified" << std::endl;
        usage();
    }

    if (conf.apikey.size() < 3 || conf.apikey.find('-') == std::string::npos) {
        std::cerr << "apikey has incorrect format" << std::endl;
        usage();
    }

    if (optind >= argc) {
        std::cerr << "Expected a command, ie 'logp run ...'" << std::endl;
        usage();
    }


    // Execute command

    std::string command(argv[optind]);

    if (command == "run") {
        logp::cmd::run(argc-optind-1, argv+optind+1);
    } else {
        std::cerr << "Unknown command: " << command << std::endl;
        usage();
    }


    return 0;
}
