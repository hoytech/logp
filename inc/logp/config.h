#pragma once

#include <string>


namespace logp {


struct config {
    std::string endpoint = "wss://ws.logperiodic.com/ws";
    std::string apikey;
    bool tls_no_verify = false;
    int verbosity = 0;
    int heartbeat_interval = 5000000;
};


bool load_config_file(std::string file, config &c);


};


extern logp::config conf;
