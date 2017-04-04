#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <poll.h>

#include <string>
#include <iostream>
#include <stdexcept>

#include "nlohmann/json.hpp"

#include "logp/websocket.h"
#include "logp/config.h"
#include "logp/util.h"


#ifndef X509_CHECK_FLAG_ALWAYS_CHECK_SUBJECT
#  error "OpenSSL 1.0.2+ required for X509_check_host() (see https://github.com/hoytech/logp/issues/1)"
#endif


namespace logp { namespace websocket {



static std::string debug_format_raw_msg(std::string msg) {
    size_t newline_loc = msg.find('\n');
    std::string header = msg.substr(0, newline_loc);
    std::string body = msg.substr(newline_loc + 1, msg.size());
    return logp::concat_string("H:", header, " B:", body);
}


std::string request::get_op_name() {
    std::string name;

    op.match(
        [&](request_ini &) {
            name = "ini";
        },
        [&](request_png &) {
            name = "png";
        },
        [&](request_get &) {
            name = "get";
        },
        [&](request_add &) {
            name = "add";
        },
        [&](request_hrt &) {
            name = "hrt";
        },
        [&](request_res &) {
            name = "res";
        }
    );

    return name;
}

std::string request::render() {
    nlohmann::json header({ { "op", get_op_name() } });
    if (request_id) header["id"] = request_id;

    nlohmann::json body({});

    op.match(
        [&](request_ini &r) {
            body = r.body;
        },
        [&](request_png &r) {
            if (r.echo.size()) body["echo"] = r.echo;
        },
        [&](request_get &r) {
            body["query"] = r.query;
            if (r.state.size()) body["state"] = r.state;
        },
        [&](request_add &r) {
            body = r.entry;
        },
        [&](request_hrt &r) {
            body["ev"] = r.event_id;
        },
        [&](request_res &r) {
            body["k"] = r.unpause_key;
        }
    );

    std::string full_msg = header.dump();
    full_msg += "\n";
    full_msg += body.dump();

    return full_msg;
}


void request::handle(nlohmann::json &body, worker *w) {
    op.match(
        [&](request_ini &) {
            throw logp::error("ini request shouldn't have gotten to handler"); // assert
        },
        [&](request_png &r) {
            if (r.on_pong) r.on_pong(body);
        },
        [&](request_get &r) {
            if (r.on_entry && body.count("e")) {
                for (auto &elem : body["e"]) {
                    r.on_entry(elem);
                }
            }

            if (body.count("s")) {
                r.state = body["s"];
                if (!r.started_monitoring && r.state["ph"] == "mn") {
                    r.started_monitoring = true;
                    r.on_monitoring();
                }
            }

            if (body.count("p")) {
                std::string unpause_key = body["p"];
                request resp;
                resp.op = request_res{unpause_key};
                resp.request_id = request_id;
                w->push_move_new_request(resp);
            }
        },
        [&](request_add &r) {
            if (r.on_ack) r.on_ack(body);
        },
        [&](request_hrt &) {},
        [&](request_res &) {}
    );
}





void worker::setup() {
    size_t dash_pos = ::conf.apikey.find('-');
    if (dash_pos == std::string::npos) throw logp::error("unable to find - in apikey");

    std::string env_id = ::conf.apikey.substr(0, dash_pos);
    token = ::conf.apikey.substr(dash_pos + 1);

    std::string endpoint = ::conf.endpoint;
    if (endpoint.back() != '/') endpoint += "/";
    endpoint += env_id;

    uri = endpoint;

    if (::conf.tls_no_verify) tls_no_verify = true;

    int rc = pipe(activity_pipe);
    if (rc) throw logp::error("unable to create pipe: ", strerror(errno));

    logp::util::make_fd_nonblocking(activity_pipe[0]);
    logp::util::make_fd_nonblocking(activity_pipe[1]);
}




void worker::push_move_new_request(request_base op) {
    request r;
    r.op = std::move(op);
    push_move_new_request(r);
}

void worker::push_move_new_request(request &r) {
    new_requests_queue.push_move(r);
    trigger_activity_pipe();
}




void worker::allocate_request_id(request &r) {
    uint64_t request_id = next_request_id++;
    r.request_id = request_id;
}

void worker::internal_send_request(connection &c, request &r) {
    std::string rendered = r.render();

    if (r.request_id) {
        active_requests.emplace(std::piecewise_construct,
                                std::forward_as_tuple(r.request_id),
                                std::forward_as_tuple(std::move(r)));
    }

    c.send_message_move(rendered);
}



void worker::run() {
    t = std::thread([this]() {
        while (1) {
            try {
                run_event_loop();
            } catch (std::exception &e) {
                PRINT_INFO << "websocket: " << e.what() << " (sleeping for " << reconnect_delay << " seconds)";
                if (on_disconnect) on_disconnect(e.what());
                logp::util::sleep_seconds(reconnect_delay);
            }
        }
    });
}


void worker::run_event_loop() {
    connection c(this);

    {
        logp::websocket::request r;
        r.op = logp::websocket::request_ini{ {{ "tk", token }, { "prot", 2 }} };
        internal_send_request(c, r);
    }

    // Re-send any live requests
    for (auto it : active_requests) {
        std::string msg = it.second.render();
        c.send_message_move(msg);
    }

    while(1) {
        struct pollfd pollfds[2];

        c.attempt_write();


        pollfds[0].fd = activity_pipe[0];
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

            ssize_t ret = read(activity_pipe[0], junk, 1);
            if (ret != 1 && (errno != EAGAIN || errno != EINTR)) {
                throw logp::error("error reading from triggering pipe: ", strerror(errno));
            }

            auto temp_queue = new_requests_queue.pop_all_no_wait();

            for (auto &req : temp_queue) {
                if (!req.request_id) allocate_request_id(req);
                internal_send_request(c, req);
            }
        }


        if (pollfds[1].revents & POLLIN) {
            c.attempt_read();
        }
    }
}



void worker::trigger_activity_pipe() {
    again:

    ssize_t ret = ::write(activity_pipe[1], "", 1);

    if (ret != 1) {
        if (errno == EINTR) goto again;
        if (errno == EAGAIN) return;
        throw logp::error("unable to write to triggering pipe: ", strerror(errno));
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
    throw logp::error("OpenSSL error ", msg, ": (", ERR_error_string(ERR_get_error(), nullptr), ")");
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
#ifdef SSL_PIN_CAFILE
            if (SSL_CTX_load_verify_locations(ctx, "/usr/logp/ssl/letsencrypt.pem", nullptr) != 1) throw_ssl_exception("unable to load pinned verify locations");
#else
            if (SSL_CTX_set_default_verify_paths(ctx) != 1) throw_ssl_exception("unable to set default verify paths");
#endif
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
    wspp_client.set_error_channels(websocketpp::log::elevel::none);

    wspp_client.register_ostream(&output_buffer_stream);

    wspp_client.set_open_handler([this](websocketpp::connection_hdl) {
        ws_connected = true;
        drain_pending_messages();
    });

    wspp_client.set_close_handler([this](websocketpp::connection_hdl hdl){
        auto con = wspp_client.get_con_from_hdl(hdl);

        throw logp::error("websocket connection closed (", con->get_ec().message(), ")");
    });

    wspp_client.set_fail_handler([this](websocketpp::connection_hdl hdl){
        auto con = wspp_client.get_con_from_hdl(hdl);

        throw logp::error("websocket handshake failure (", con->get_ec().message(), ")");
    });

    wspp_client.set_message_handler([this](websocketpp::connection_hdl, websocketpp::client<websocketpp::config::core>::message_ptr msg) {
        PRINT_DEBUG << "RECV: " << debug_format_raw_msg(msg->get_payload());

        std::stringstream ss(msg->get_payload());

        uint64_t request_id = 0;
        bool fin = false;
        std::string header_str;
        std::string body_str;

        try {
            std::getline(ss, header_str);

            auto json = nlohmann::json::parse(header_str);

            if (json.count("id")) {
                request_id = json["id"];
                if (request_id == 0) throw logp::error("unexpected request_id of 0");
            }

            if (json.count("fin")) fin = json["fin"];

            std::getline(ss, body_str);
        } catch (std::exception &e) {
            PRINT_WARNING << "unable to parse websocket header, ignoring: " << e.what();
            PRINT_DEBUG << "header = " << header_str;
            return;
        }

        try {
            auto json = nlohmann::json::parse(body_str);

            if (request_id == 0) {
                if (json.count("err")) {
                    std::string err = json["err"];
                    PRINT_WARNING << "connection-level error from server: " << err;
                }

                if (json.count("ini")) {
                    if (parent_worker->on_ini_response) parent_worker->on_ini_response(json);

                    if (json["ini"] == "ok") {
                        uint64_t permissions = json["perm"];
                        uint64_t protocol = json["prot"];
                        PRINT_DEBUG << "ini request OK, permissions = " << permissions << ", protocol = " << protocol;
                    }
                }

                return;
            }

            auto find_res = parent_worker->active_requests.find(request_id);

            if (find_res == parent_worker->active_requests.end()) {
                PRINT_WARNING << "response came for unknown request id, ignoring";
                return;
            }

            auto &req = find_res->second;

            if (json.count("err")) {
                std::string err = json["err"];
                PRINT_WARNING << "error from server for " << req.get_op_name() << " op: " << err;
            } else {
                req.handle(json, parent_worker);
            }
        } catch (std::exception &e) {
            PRINT_WARNING << "unable to parse websocket body, ignoring: " << e.what();
            PRINT_DEBUG << "body = " << body_str;
            return;
        }

        if (fin) {
            parent_worker->active_requests.erase(request_id);
        }
    });

    websocketpp::lib::error_code ec;
    std::string uri_str = uri.str();
    wspp_conn = wspp_client.get_connection(uri_str, ec);

    wspp_client.connect(wspp_conn);

    PRINT_INFO << "connected to endpoint: " << parent_worker->uri;
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
    PRINT_DEBUG << "SEND: " << debug_format_raw_msg(msg);
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
        throw logp::error("error sending to websocket: ", e.message());
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

    throw logp::error(err);
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
                throw logp::error("tls error");
            }
        } else if (ret == 0) {
            throw logp::error("socket closed");
        }

        bytes_written = ret;
    } else {
        ssize_t ret = ::write(connection_fd, output_buffer.data(), output_buffer.size());

        if (ret == -1) {
            if (errno == EAGAIN || errno == EINTR) return;
            throw logp::error("error writing to socket: ", strerror(errno));
        } else if (ret == 0) {
            throw logp::error("socket closed");
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
                throw logp::error("tls error");
            }
        } else if (ret == 0) {
            throw logp::error("socket closed");
        }

        bytes_read = ret;
    } else {
        ssize_t ret = ::read(connection_fd, buf, sizeof(buf));

        if (ret == -1) {
            if (errno == EAGAIN || errno == EINTR) return;
            throw logp::error("error reading from socket: ", strerror(errno));
        } else if (ret == 0) {
            throw logp::error("socket closed");
        }

        bytes_read = ret;
    }

    wspp_conn->read_some(buf, bytes_read);
}


}}
