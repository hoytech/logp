#pragma once

#include <string>
#include <thread>
#include <sstream>
#include <vector>
#include <functional>
#include <unordered_map>

#include <openssl/ssl.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/x509.h>

#include "websocketpp/config/core.hpp"
#include "websocketpp/client.hpp"
#include "websocketpp/uri.hpp"
#include "protected_queue/protected_queue.h"

#include "logp/openssl_hostname_validation.h"
#include "logp/messages.h"


namespace logp { namespace websocket {



class request {
  public:
    request(uint64_t request_id_, std::function<void(std::string &)> cb_) : request_id(request_id_), cb(cb_) {}

    uint64_t request_id;
    std::function<void(std::string &)> cb;
};




class worker {
  public:
    worker(std::string &uri_raw, std::string &token_) : uri(uri_raw), token(token_) {
        setup();
    }

    void run();
    void send_message_move(std::string &op, std::string &msg, std::function<void(std::string &)> cb);

    websocketpp::uri uri;
    std::string token;

    // Accessed by connection
    bool tls_no_verify = false;
    std::unordered_map<uint64_t, request> active_requests;

  private:
    void setup();
    void run_event_loop();
    void trigger_input_queue();

    std::thread t;
    uint64_t next_request_id = 1;
    int input_queue_activity_pipe[2];
    hoytech::protected_queue<logp::msg::websocket_input> input_queue;
};




class connection {
  public:
    connection(worker *parent_worker_) : parent_worker(parent_worker_) {
        setup();
    }
    ~connection();

    void send_message_move(std::string &msg);

    void attempt_write();
    void attempt_read();
    bool want_write();
    bool want_read();

    websocketpp::client<websocketpp::config::core> wspp_client;
    websocketpp::client<websocketpp::config::core>::connection_ptr wspp_conn;
    int connection_fd = -1;

  private:
    void setup();
    void open_socket(websocketpp::uri &uri);

    worker *parent_worker;

    bool ws_connected = false;
    std::vector<std::string> pending_messages;
    void drain_pending_messages();

    std::stringstream output_buffer_stream;
    std::string output_buffer;

    // OpenSSL stuff
    bool use_tls = false;
    bool tls_want_read = false;
    bool tls_want_write = false;
    SSL_CTX *ctx = nullptr;
    SSL *ssl = nullptr;
    BIO *bio = nullptr;
};



}}
