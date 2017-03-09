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
#include "websocketpp/extensions/permessage_deflate/enabled.hpp"
#include "websocketpp/uri.hpp"
#include "hoytech/protected_queue.h"






namespace logp { namespace websocket {



struct my_websocketpp_config : public websocketpp::config::core {
    struct permessage_deflate_config {};

    typedef websocketpp::extensions::permessage_deflate::enabled
        <permessage_deflate_config> permessage_deflate_type;
};



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
    }

    void push_move_new_request(request &r);
    void run();
    std::function<void(nlohmann::json &)> on_ini_response;
    std::function<void(std::string reason)> on_disconnect;
    int reconnect_delay = 2;

    std::string uri;
    std::string token;
    bool tls_no_verify = false;

  private:
    friend class connection;

    void setup();
    void run_event_loop();
    void trigger_activity_pipe();
    std::string prepare_new_request(request &req);
    std::string prepare_new_request(request &req, uint64_t request_id);
    std::string render_request(request &req);

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

    websocketpp::client<my_websocketpp_config> wspp_client;
    websocketpp::client<my_websocketpp_config>::connection_ptr wspp_conn;
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
