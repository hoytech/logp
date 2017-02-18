#include <iostream>
#include <string>
#include <stdexcept>

#include "yaml-cpp/yaml.h"

#include "logp/config.h"
#include "logp/print.h"


namespace logp {


bool load_config_file(std::string file, config &c) {
    YAML::Node node;

    try {
        node = YAML::LoadFile(file);
    } catch (YAML::BadFile &e) {
        return false;
    }

    for(auto it=node.begin(); it!=node.end(); it++) {
        std::string k = it->first.as<std::string>();
        auto &v = it->second;

        if (k == "endpoint") {
            c.endpoint = v.as<std::string>();
        } else if (k == "apikey") {
            c.apikey = v.as<std::string>();
        } else if (k == "tls_no_verify") {
            c.tls_no_verify = v.as<bool>();
        } else {
            PRINT_INFO << "warning: Unrecognized config option (" + k + ") in file " + file;
        }
    }

    return true;
}



}
