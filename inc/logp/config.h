#pragma once

#include <string>


namespace logp {


struct config {
    std::string endpoint = "wss://ws.logperiodic.com";
    std::string apikey;
    bool tls_no_verify = false;
};


bool load_config_file(std::string file, config &c);


};


extern logp::config conf;
