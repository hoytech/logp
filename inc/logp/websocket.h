#pragma once

#include <string>
#include <thread>
#include <sstream>
#include <vector>
#include <functional>

#include "websocketpp/config/core.hpp"
#include "websocketpp/client.hpp"
#include "websocketpp/uri.hpp"
#include "protected_queue/protected_queue.h"

#include "logp/messages.h"


namespace logp { namespace websocket {


enum class tls_mode { SECURE, NO_VERIFY };

class worker {
  public:
    worker(std::string &uri_raw) : uri(uri_raw) {
        setup();
    }

    void run();
    send_message_move(std::string &msg, std::function<void()> cb);

    tls_mode my_tls_mode = tls_mode::SECURE;

  private:
    void setup();
    void run_event_loop();
    void trigger_input_queue();

    websocketpp::uri uri;

    std::thread t;
    uint64_t next_request_id = 1;
    int input_queue_activity_pipe[2];
    hoytech::protected_queue<logp::msg::websocket_input> input_queue;
};



class connection {
  public:
    connection(websocketpp::uri &uri);
    ~connection();

    void send_message_move(std::string &msg);

    bool attempt_write(); // returns true if there is more to be written
    void attempt_read();

    websocketpp::client<websocketpp::config::core> wspp_client;
    websocketpp::client<websocketpp::config::core>::connection_ptr wspp_conn;
    int connection_fd = -1;

  private:
    void open_socket(websocketpp::uri &uri);

    bool ws_connected = false;
    std::vector<std::string> pending_messages;
    void drain_pending_messages();

    std::stringstream output_buffer_stream;
    std::string output_buffer;
};



}}
