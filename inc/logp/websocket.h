#pragma once

#include <string>


namespace logp { namespace websocket {


enum class tls_type { SECURE, NO_VERIFY };

class connection {
  public:
    connection(std::string &uri, tls_type tls = tls_type::SECURE);
};



}}
