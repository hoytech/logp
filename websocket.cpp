#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <poll.h>

#include <iostream>
#include <stdexcept>

#include "nlohmann/json.hpp"

#include "logp/util.h"
#include "logp/websocket.h"
#include "logp/config.h"
#include "logp/print.h"




namespace logp { namespace websocket {



void worker::setup() {
    size_t dash_pos = ::conf.apikey.find('-');
    if (dash_pos == std::string::npos) throw std::runtime_error(std::string("unable to find - in apikey"));

    std::string env_id = ::conf.apikey.substr(0, dash_pos + 1);
    token = ::conf.apikey.substr(dash_pos + 1);

    std::string endpoint = ::conf.endpoint;
    if (endpoint.back() != '/') endpoint += "/";
    endpoint += env_id;

    uri = endpoint;

    if (::conf.tls_no_verify) tls_no_verify = true;

    int rc = pipe(input_queue_activity_pipe);
    if (rc) throw std::runtime_error(std::string("unable to create pipe: ") + strerror(errno));

    logp::util::make_fd_nonblocking(input_queue_activity_pipe[0]);
    logp::util::make_fd_nonblocking(input_queue_activity_pipe[1]);
}


void worker::send_message_move(std::string &op, std::string &msg, std::function<void(std::string &)> cb) {
    uint64_t request_id = next_request_id++;

    active_requests.emplace(std::piecewise_construct,
                            std::forward_as_tuple(request_id),
                            std::forward_as_tuple(request_id, cb));

    nlohmann::json j = {{ "id", request_id }, { "op", op }, { "tk", token }};
    std::string full_msg = j.dump();
    full_msg += "\n";
    full_msg += msg;

    input_queue.push_move(full_msg);
    trigger_input_queue();
}


void worker::run() {
    t = std::thread([this]() {
        while (1) {
            try {
                run_event_loop();
            } catch (std::exception &e) {
                PRINT_WARNING << "websocket: " << e.what() << " (sleeping and trying again)";
                sleep(5);
            }
        }
    });
}


void worker::run_event_loop() {
    connection c(this);

    while(1) {
        struct pollfd pollfds[2];

        c.attempt_write();


        pollfds[0].fd = input_queue_activity_pipe[0];
        pollfds[0].events = POLLIN;

        pollfds[1].fd = c.connection_fd;
        pollfds[1].events = (c.want_read() ? POLLIN : 0) | (c.want_write() ? POLLOUT : 0);


        int rc = poll(pollfds, 2, -1);
        if (rc == -1) {
            if (errno != EINTR) PRINT_WARNING << "warning: couldn't poll: " << strerror(errno);
            continue;
        }


        if (pollfds[0].revents & POLLIN) {
            char junk[1];

            ssize_t ret = read(input_queue_activity_pipe[0], junk, 1);
            if (ret != 1 && (errno != EAGAIN || errno != EINTR)) {
                throw std::runtime_error(std::string("error reading from triggering pipe: ") + strerror(errno));
            }

            auto temp_queue = input_queue.pop_all_no_wait();

            for (auto &inp : temp_queue) {
                c.send_message_move(inp.data);
            }
        }


        if (pollfds[1].revents & POLLIN) {
            c.attempt_read();
        }
    }
}



void worker::trigger_input_queue() {
    again:

    ssize_t ret = ::write(input_queue_activity_pipe[1], "", 1);

    if (ret != 1) {
        if (errno == EINTR) goto again;
        if (errno == EAGAIN) return;
        throw std::runtime_error(std::string("unable to write to triggering pipe: ") + strerror(errno));
    }
}




void init_openssl_library() {
    static bool initialized = false;

    if (initialized) return;

    (void)SSL_library_init();
    SSL_load_error_strings();

    initialized = true;
}


static void throw_ssl_exception(std::string msg) {
    throw std::runtime_error(std::string("OpenSSL error ") + msg + ": (" + std::string(ERR_error_string(ERR_get_error(), nullptr)) + ")");
}


void connection::setup() {
    websocketpp::uri orig_uri(parent_worker->uri);
    use_tls = (orig_uri.get_scheme() == "wss");
    websocketpp::uri uri(false, orig_uri.get_host(), orig_uri.get_port(), orig_uri.get_resource());

    open_socket(uri);

    if (use_tls) {
        init_openssl_library();

        const SSL_METHOD* method = SSLv23_method();
        if (!method) throw_ssl_exception("unable to load method");

        ctx = SSL_CTX_new(method);
        if (!ctx) throw_ssl_exception("unable to create context");

        if (!parent_worker->tls_no_verify) {
            if (SSL_CTX_set_default_verify_paths(ctx) != 1) throw_ssl_exception("unable to set default verify paths");
            SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);
            SSL_CTX_set_verify_depth(ctx, 4);
        }

        SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_COMPRESSION);

        ssl = SSL_new(ctx);
        if (!ssl) throw_ssl_exception("unable to create new SSL instance");

        if (SSL_set_cipher_list(ssl, "HIGH:!aNULL:!kRSA:!PSK:!SRP:!MD5:!RC4") != 1) throw_ssl_exception("unable to set cipher list");
        if (SSL_set_tlsext_host_name(ssl, orig_uri.get_host().c_str()) != 1) throw_ssl_exception("unable to set hostname"); // For SNI
        SSL_set_mode(ssl, SSL_MODE_AUTO_RETRY);

        bio = BIO_new_socket(connection_fd, BIO_NOCLOSE);
        if (!bio) throw_ssl_exception("unable to create BIO");
        SSL_set_bio(ssl, bio, bio);

        int ret = SSL_connect(ssl);
        if (ret <= 0) {
             throw_ssl_exception("handshake failure");
        }

        if (!parent_worker->tls_no_verify) {
            if (SSL_get_verify_result(ssl) != X509_V_OK) throw_ssl_exception("couldn't verify certificate");

            std::string host = uri.get_host();

            X509 *server_cert =  SSL_get_peer_certificate(ssl);
            if (!server_cert) throw_ssl_exception("couldn't find server cert");

            int check_ret = X509_check_host(server_cert, host.c_str(), host.size(), X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS, nullptr);

            X509_free(server_cert);

            if (check_ret != 1) throw_ssl_exception("couldn't verify hostname");
        }

        SSL_set_mode(ssl, SSL_get_mode(ssl) & (~SSL_MODE_AUTO_RETRY));
    }

    logp::util::make_fd_nonblocking(connection_fd);


    wspp_client.set_access_channels(websocketpp::log::alevel::none);

    wspp_client.register_ostream(&output_buffer_stream);

    wspp_client.set_open_handler([this](websocketpp::connection_hdl) {
        ws_connected = true;
        drain_pending_messages();
    });

    wspp_client.set_message_handler([this](websocketpp::connection_hdl, websocketpp::client<websocketpp::config::core>::message_ptr msg) {
        std::stringstream ss(msg->get_payload());

        uint64_t request_id;
        bool fin = false;
        std::string body_str;

        try {
            std::string header_str;
            std::getline(ss, header_str);

            auto json = nlohmann::json::parse(header_str);

            request_id = json["id"];
            if (json.count("fin")) fin = json["fin"];

            std::getline(ss, body_str);
        } catch (std::exception &e) {
            PRINT_WARNING << "unable to parse websocket header, ignoring: " << e.what();
            return;
        }

        auto res = parent_worker->active_requests.find(request_id);

        if (res == parent_worker->active_requests.end()) {
            PRINT_WARNING << "response came for unknown request id, ignoring";
            return;
        }

        res->second.cb(body_str);
    });

    websocketpp::lib::error_code ec;
    std::string uri_str = uri.str();
    wspp_conn = wspp_client.get_connection(uri_str, ec);

    wspp_client.connect(wspp_conn);
}



connection::~connection() {
    if (ssl) SSL_free(ssl);
    ssl = nullptr;
    if (ctx) SSL_CTX_free(ctx);
    ctx = nullptr;

    if (connection_fd != -1) close(connection_fd);
    connection_fd = -1;
}



void connection::send_message_move(std::string &msg) {
    pending_messages.emplace_back(std::move(msg));

    drain_pending_messages();
}


void connection::drain_pending_messages() {
    if (!ws_connected) return;

    try {
        for (auto &msg : pending_messages) {
            wspp_client.send(wspp_conn, msg, websocketpp::frame::opcode::text);
        }
    } catch (const websocketpp::lib::error_code& e) {
        throw std::runtime_error(std::string("error sending to websocket: ") + e.message());
    };

    pending_messages.clear();
}



void connection::open_socket(websocketpp::uri &uri) {
    struct addrinfo hints, *res = NULL;
    int fd = -1;
    std::string err;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int rc = ::getaddrinfo(uri.get_host().c_str(), std::to_string(uri.get_port()).c_str(), &hints, &res);
    if (rc) {
        err = "can't lookup host"; //FIXME: include strerrors on this and below
        goto bail;
    }

    fd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd == -1) {
        err = "can't create socket";
        goto bail;
    }

    rc = ::connect(fd, res->ai_addr, res->ai_addrlen);
    if (rc) {
        err = "can't connect";
        goto bail;
    }

    freeaddrinfo(res);
    connection_fd = fd;
    return;

    bail:

    if (res) freeaddrinfo(res);
    if (fd != -1) close(fd);

    throw std::runtime_error(err);
}



bool connection::want_write() {
    if (tls_want_read) return false;
    return !!output_buffer.size();
}

bool connection::want_read() {
    if (tls_want_write) return false;
    return true;
}


void connection::attempt_write() {
    std::string new_contents = output_buffer_stream.str();

    if (new_contents.size()) {
        output_buffer_stream.str("");
        output_buffer += new_contents;
    }

    if (!output_buffer.size()) return;

    size_t bytes_written = 0;

    if (use_tls) {
        tls_want_read = tls_want_write = false;

        int ret = (ssize_t) SSL_write(ssl, output_buffer.data(), output_buffer.size());

        if (ret < 0) {
            int err = SSL_get_error(ssl, ret);

            if (err == SSL_ERROR_WANT_READ) {
                tls_want_read = true;
                return;
            } else if (err == SSL_ERROR_WANT_WRITE) {
                tls_want_write = true;
                return;
            } else {
                throw std::runtime_error(std::string("tls error"));
            }
        } else if (ret == 0) {
            throw std::runtime_error(std::string("socket closed"));
        }

        bytes_written = ret;
    } else {
        ssize_t ret = ::write(connection_fd, output_buffer.data(), output_buffer.size());

        if (ret == -1) {
            if (errno == EAGAIN || errno == EINTR) return;
            throw std::runtime_error(std::string("error writing to socket: ") + strerror(errno));
        } else if (ret == 0) {
            throw std::runtime_error(std::string("socket closed"));
        }

        bytes_written = ret;
    }

    output_buffer.erase(0, bytes_written);
}


void connection::attempt_read() {
    char buf[4096];

    size_t bytes_read = 0;

    if (use_tls) {
        tls_want_read = tls_want_write = false;

        int ret = SSL_read(ssl, buf, sizeof(buf));

        if (ret < 0) {
            int err = SSL_get_error(ssl, ret);

            if (err == SSL_ERROR_WANT_READ) {
                tls_want_read = true;
                return;
            } else if (err == SSL_ERROR_WANT_WRITE) {
                tls_want_write = true;
                return;
            } else {
                throw std::runtime_error(std::string("tls error"));
            }
        } else if (ret == 0) {
            throw std::runtime_error(std::string("socket closed"));
        }

        bytes_read = ret;
    } else {
        ssize_t ret = ::read(connection_fd, buf, sizeof(buf));

        if (ret == -1) {
            if (errno == EAGAIN || errno == EINTR) return;
            throw std::runtime_error(std::string("error reading from socket: ") + strerror(errno));
        } else if (ret == 0) {
            throw std::runtime_error(std::string("socket closed"));
        }

        bytes_read = ret;
    }

    wspp_conn->read_some(buf, bytes_read);
}


}}
