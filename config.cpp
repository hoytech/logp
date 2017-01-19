#include <iostream>
#include <string>
#include <stdexcept>

#include "yaml-cpp/yaml.h"

#include "logp/config.h"


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
        std::string v = it->second.as<std::string>();

        if (k == "endpoint") {
            c.endpoint = v;
        } else if (k == "apikey") {
            c.apikey = v;
        } else {
            std::cerr << "logp: warning: Unrecognized config option (" + k + ") in file " + file << std::endl;
        }
    }

    return true;
}



}
