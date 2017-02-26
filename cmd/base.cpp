#include <unistd.h>

#include <vector>
#include <iostream>
#include <stdexcept>

#include "logp/cmd/base.h"


extern const char *logp_global_usage_string;


namespace logp { namespace cmd {


void base::parse_params(int argc, char **argv) {
    auto *long_options = get_long_options();

    my_long_options.push_back({"help", no_argument, 0, 'h'});

    while (long_options[0].name || long_options[0].val) {
        my_long_options.push_back(long_options[0]);
        long_options++;
    }

    my_long_options.push_back({ 0,0,0,0 });


    std::string optstr = "+h?";
    optstr += getopt_string();


    int arg, option_index;

    optind = 1;
    while ((arg = getopt_long(argc, argv, optstr.c_str(), my_long_options.data(), &option_index)) != -1) {
        if (arg == 'h' || arg == '?') print_usage_and_exit();
        process_option(arg, option_index, optarg);
    }

    my_argv = argv;
}

void base::print_usage_and_exit() {
    std::cerr << ::logp_global_usage_string << "Usage: " << usage() << std::endl;
    exit(1);
}

}}
