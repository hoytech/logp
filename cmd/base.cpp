#include <unistd.h>

#include <vector>
#include <iostream>
#include <stdexcept>

#include "logp/cmd/base.h"


namespace logp { namespace cmd {


void base::parse_params(int argc, char **argv) {
    auto *long_options = get_long_options();

    std::vector<struct option> new_long_options;

    new_long_options.push_back({"help", no_argument, 0, 'h'});

    while (long_options[0].name || long_options[0].val) {
        new_long_options.push_back(long_options[0]);
        long_options++;
    }

    new_long_options.push_back({ 0,0,0,0 });


    int arg, option_index;

    optind = 1;
    while ((arg = getopt_long_only(argc, argv, "+", new_long_options.data(), &option_index)) != -1) {
        if (arg == 'h' || arg == '?') print_usage_and_exit();
        process_option(arg);
    }

    my_argv = argv;
}


void base::print_usage_and_exit() {
    std::cerr << "\nUsage: " << usage() << std::endl;
    exit(1);
}

}}
