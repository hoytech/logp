#pragma once

#include <string>


namespace logp {


struct config {
    std::string url;
    std::string token;
};


bool load_config_file(std::string file, config &c);


};


extern logp::config conf;
