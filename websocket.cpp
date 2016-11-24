#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <poll.h>

#include <iostream>
#include <stdexcept>

#include "logp/util.h"
#include "logp/websocket.h"




namespace logp { namespace websocket {



void worker::setup() {
    int rc = pipe(input_queue_activity_pipe); // FIXME: make nonblocking

    if (rc) throw std::runtime_error(std::string("unable to create pipe: ") + strerror(errno));

    logp::util::make_fd_nonblocking(input_queue_activity_pipe[0]);
    logp::util::make_fd_nonblocking(input_queue_activity_pipe[1]);
}


void worker::run() {
    std::cerr << "HOST = " << uri.get_port() << std::endl;

    t = std::thread([this]() {
        while (1) {
            try {
                run_event_loop();
            } catch (std::exception &e) {
                std::cerr << "Websocket error: " << e.what() << " (sleeping and trying again)" << std::endl;
                sleep(5);
            }
        }
    });
}


void worker::run_event_loop() {
    connection c(uri);

    while(1) {
        struct pollfd pollfds[2];
        bool want_write = c.attempt_write();


        pollfds[0].fd = input_queue_activity_pipe[0];
        pollfds[0].events = POLLIN;

        pollfds[1].fd = c.connection_fd;
        pollfds[1].events = POLLIN | (want_write ? POLLOUT : 0);


        int rc = poll(pollfds, 2, -1);
        if (rc == -1) {
            std::cerr << "logp: WARNING: couldn't poll" << std::endl;
            continue;
        }


        if (pollfds[0].revents & POLLIN) {
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






connection::connection(websocketpp::uri &uri) {
    open_socket(uri);

    wspp_client.set_access_channels(websocketpp::log::alevel::all);
    wspp_client.clear_access_channels(websocketpp::log::alevel::frame_payload);

    wspp_client.register_ostream(&output_buffer_stream);

    wspp_client.set_open_handler([this](websocketpp::connection_hdl) {
        ws_connected = true;
        drain_pending_messages();
    });

    wspp_client.set_message_handler([this](websocketpp::connection_hdl, websocketpp::client<websocketpp::config::core>::message_ptr) {
        std::cerr << "BING" << std::endl;
    });

    websocketpp::lib::error_code ec;
    std::string uri_str = uri.str();
    wspp_conn = wspp_client.get_connection(uri_str, ec);

    wspp_client.connect(wspp_conn);
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

    logp::util::make_fd_nonblocking(fd);

    freeaddrinfo(res);
    connection_fd = fd;
    return;

    bail:

    if (res) freeaddrinfo(res);
    if (fd != -1) close(fd);

    throw err;
    throw std::runtime_error(err);
}


connection::~connection() {
    if (connection_fd != -1) close(connection_fd);
    connection_fd = -1;
}



bool connection::attempt_write() {
    std::string new_contents = output_buffer_stream.str();

    if (new_contents.size()) {
        output_buffer_stream.str("");
        output_buffer += new_contents;
    }

    if (output_buffer.size()) {
        ssize_t ret = ::write(connection_fd, output_buffer.data(), output_buffer.size());

        if (ret == -1) {
            if (errno == EAGAIN || errno == EINTR) return true;
            throw std::runtime_error(std::string("error writing to socket: ") + strerror(errno));
        } else if (ret == 0) {
            throw std::runtime_error(std::string("socket closed"));
        }

        output_buffer.erase(0, ret);
    }

    return !!output_buffer.size();
}


void connection::attempt_read() {
    char buf[4096];

    ssize_t ret = ::read(connection_fd, buf, sizeof(buf));

    if (ret == -1) {
        if (errno == EAGAIN || errno == EINTR) return;
        throw std::runtime_error(std::string("error reading from socket: ") + strerror(errno));
    } else if (ret == 0) {
        throw std::runtime_error(std::string("socket closed"));
    }

std::cerr << "READ: " << std::string(buf, ret) << std::endl;
    wspp_conn->read_some(buf, ret);
}


}}
