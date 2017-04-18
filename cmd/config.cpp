#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include <iostream>
#include <string>

#include "nlohmann/json.hpp"

#include "logp/cmd/config.h"
#include "logp/util.h"


namespace logp { namespace cmd {

const char *config::getopt_string() { return ""; }

struct option *config::get_long_options() {
    static struct option opts[] = {
        {0, 0, 0, 0}
    };

    return opts;
}

void config::process_option(int, int, char *) {
}

const char *config::usage() {
    static const char *u =
        "logp config\n"
    ;

    return u;
}

void config::execute() {
    std::cout << "Loaded config from file: " << conf.file << "\n\n";
    std::cout << conf.tree.dump(4) << std::endl;
}

}}
