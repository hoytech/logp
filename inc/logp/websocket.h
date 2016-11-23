#pragma once

#include <string>
#include <thread>

#include "websocketpp/config/core.hpp"
#include "websocketpp/client.hpp"
#include "websocketpp/uri.hpp"
#include "protected_queue/protected_queue.h"

#include "logp/messages.h"


namespace logp { namespace websocket {


enum class tls_type { SECURE, NO_VERIFY };

class connection {
  public:
    connection(std::string &uri_) : uri(uri_) {}

    void start();
    void trigger_input_queue();

    tls_type tls_mode = tls_type::SECURE;
    hoytech::protected_queue<logp::msg::websocket_input> input_queue;

  private:
    void run();
    void connect();

    websocketpp::uri uri;
    websocketpp::client<websocketpp::config::core> wspp_client;

    uint64_t next_request_id = 1;
    int connection_fd = -1;
    int input_queue_activity_pipe[2];
    std::thread t;
};



}}
