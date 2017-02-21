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
#include <openssl/x509v3.h>

#include "nlohmann/json.hpp"

#include "websocketpp/config/core.hpp"
#include "websocketpp/client.hpp"
#include "websocketpp/uri.hpp"
#include "protected_queue/protected_queue.h"


namespace logp { namespace websocket {




class request {
  public:
    std::string render_body();

    std::string op;
    nlohmann::json body;
    std::function<void(nlohmann::json &)> on_data;
    std::function<void()> on_finished_history;

    uint64_t request_id = 0;
    uint64_t latest_entry = 0;
};



class worker {
  public:
    worker() {
        setup();
        run();
    }

    void push_move_new_request(request &r);

  private:
    friend class connection;

    void setup();
    void run();
    void run_event_loop();
    void trigger_activity_pipe();
    std::string prepare_new_request(request &req);
    std::string render_request(request &req);

    std::string uri;
    std::string token;
    bool tls_no_verify = false;

    std::thread t;
    uint64_t next_request_id = 1;
    int activity_pipe[2];
    hoytech::protected_queue<request> new_requests_queue;

    std::unordered_map<uint64_t, request> active_requests;
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
