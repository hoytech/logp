#pragma once

#include <string>
#include <thread>

#include "protected_queue/protected_queue.h"

#include "logp/messages.h"


namespace logp { namespace websocket {


enum class tls_type { SECURE, NO_VERIFY };

class connection {
  public:
    connection(std::string &uri);

    hoytech::protected_queue<logp::msg::websocket_input> inp_queue;
    std::thread t;
};



}}
