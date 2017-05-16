#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <stdlib.h>

#include <iostream>
#include <string>

#include "logp/websocket.h"
#include "logp/config.h"
#include "logp/util.h"

#include "logp/cmd/base.h"
#include "logp/cmd/run.h"
#include "logp/cmd/ps.h"
#include "logp/cmd/ping.h"
#include "logp/cmd/get.h"
#include "logp/cmd/tail.h"
#include "logp/cmd/config.h"


logp::config conf;


#include "_buildinfo.h"


const char *logp_global_usage_string =
    "\nlogp v" LOGP_VERSION " -- Command-line client for Log Periodic\n"
    "Copyright (C) Log Periodic Ltd.  https://logperiodic.com\n\n"
  ;



void usage() {
    std::cerr << logp_global_usage_string <<
        "Usage: logp [global options] <command> [command options]\n"
        "\n"
        "  Global options:\n"
        "    --config/-c [file]   Use specified config file\n"
        "    --profile/-p [name]  Profile to use from the config\n"
        "    --apikey/-a [key]    API key to use\n"
        "    --verbose/-v         More diagnostic messages on stderr\n"
        "    --quiet/-q           Fewer diagnostic messages on stderr\n"
        "    --version            Print logp version and exit\n"
        "    --help               The message you are reading now\n"
        "\n"
        "  Environment variables:\n"
        "    LOGP_APIKEY     API key to use (-a takes precedence)\n"
        "\n"
        "  Commands:\n"
        "    run     Execute the given command, upload information\n"
        "    ping    Test your apikey works, check latency to LP servers\n"
        "    ps      See what is currently running, follow new runs\n"
        "    tail    Print stdout/stderr of an event\n"
        << std::endl;
    exit(1);
}


int main(int argc, char **argv) {
    if (::isatty(1)) logp::util::use_ansi_colours = true;

    // Argument parsing

    int arg, option_index;

    std::string opt_config;
    std::string opt_apikey;

    struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"version", no_argument, 0, 0},
        {"verbose", no_argument, 0, 'v'},
        {"quiet", no_argument, 0, 'q'},
        {"config", required_argument, 0, 'c'},
        {"profile", required_argument, 0, 'p'},
        {"apikey", required_argument, 0, 'a'},
        {0, 0, 0, 0}
    };

    optind = 1;
    while ((arg = getopt_long(argc, argv, "+c:p:a:vq", long_options, &option_index)) != -1) {
        switch (arg) {
          case '?':
          case 'h':
            usage();

          case 'c':
            opt_config = std::string(optarg);
            break;

          case 'p':
            conf.profile = std::string(optarg);
            break;

          case 'a':
            opt_apikey = std::string(optarg);
            break;

          case 'v':
            ::conf.verbosity++;
            break;

          case 'q':
            ::conf.verbosity--;
            break;

          case 0:
            if (strcmp(long_options[option_index].name, "version") == 0) {
                std::cout << "logp v" LOGP_VERSION << std::endl;
                exit(0);
            }

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

    if (!config_loaded) config_loaded = logp::load_config_file("./.logp.conf", conf);

    if (!config_loaded) {
        std::string file = logp::util::get_home_dir();

        if (file.size()) {
            file += "/.logp.conf";
            config_loaded = logp::load_config_file(file, conf);
        } else {
            // unable to determine home directory
        }
    }

    if (!config_loaded) config_loaded = logp::load_config_file("/etc/logp.conf", conf);

    if (conf.profile.size()) {
        if (!config_loaded) {
            PRINT_ERROR << "Profile specified but no config file could be found";
            exit(1);
        }

        if (!conf.tree.count("profiles") || !conf.tree["profiles"].count(conf.profile)) {
            PRINT_ERROR << "Unable to find profile '" << conf.profile << "' in config file '" << conf.file << "'";
            exit(1);
        }
    }

    if (opt_apikey.size()) {
        conf.tree["apikey"] = opt_apikey;
    } else if (getenv("LOGP_APIKEY")) {
        conf.tree["apikey"] = std::string(getenv("LOGP_APIKEY"));
    }


    // Verify necessary settings

    if (optind >= argc) {
        PRINT_ERROR << "Expected a command, ie 'logp run sleep 10'";
        usage();
    }


    // Execute command

    std::string command(argv[optind]);

    logp::cmd::base *c = nullptr;

    if (command == "run") {
        c = new logp::cmd::run();
    } else if (command == "ps") {
        c = new logp::cmd::ps();
    } else if (command == "ping") {
        c = new logp::cmd::ping();
    } else if (command == "get") {
        c = new logp::cmd::get();
    } else if (command == "tail") {
        c = new logp::cmd::tail();
    } else if (command == "config") {
        c = new logp::cmd::config();
    }

    if (c) {
        try {
            c->parse_params(argc-optind, argv+optind);
        } catch (std::exception &e) {
            PRINT_ERROR << "failure parsing params: " << e.what();
            exit(1);
        };

        try {
            c->execute();
        } catch (std::exception &e) {
            PRINT_ERROR << "failure executing: " << e.what();
            exit(1);
        };
    } else {
        PRINT_ERROR << "Unknown command: " << command;
        usage();
    }

    return 0;
}
