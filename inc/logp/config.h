#pragma once

#include <string.h>

#include <string>

#include "nlohmann/json.hpp"



namespace logp {


struct config {
    nlohmann::json tree;
    int verbosity = 0; // FIXME: doesn't really belong here

    nlohmann::json *get_node(std::string name) { return get_node(name, tree); };
    nlohmann::json *get_node(std::string name, nlohmann::json &j);
    bool get_bool(std::string name, bool default_val);
    std::string get_str(std::string name, std::string default_val);
    uint64_t get_uint64(std::string name, uint64_t default_val);
};


bool load_config_file(std::string file, config &c);




};


extern logp::config conf;
