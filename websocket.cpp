#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <poll.h>

#include <iostream>
#include <stdexcept>

#include "logp/websocket.h"




using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;



namespace logp { namespace websocket {


void connection::start() {
    int rc = pipe(&input_queue_activity_pipe[0]);

    if (rc) throw std::runtime_error(std::string("unable to create pipe: ") + strerror(errno));

    std::cerr << "HOST = " << uri.get_port() << std::endl;

    t = std::thread([this]() { run(); });
}


void connection::run() {
    while(1) {
        if (connection_fd == -1) {
            connect();

            if (connection_fd == -1) {
                sleep(5);
                continue;
            }
        }

        struct pollfd pollfds[2];

        pollfds[0].fd = input_queue_activity_pipe[0];
        pollfds[0].events = POLLIN;

        pollfds[1].fd = connection_fd;
        pollfds[1].events = POLLIN;

        int rc = poll(pollfds, 2, -1);
        if (rc == -1) {
            std::cerr << "logp: WARNING: couldn't poll" << std::endl;
            continue;
        }

        std::cerr << "THREAD" << std::endl;
        sleep(1);
    }
}


void connection::connect() {
    if (connection_fd != -1) {
        close(connection_fd);
        connection_fd = -1;
    }

    struct addrinfo hints, *res;
    int fd = -1;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int rc = ::getaddrinfo(uri.get_host().c_str(), std::to_string(uri.get_port()).c_str(), &hints, &res);
    if (rc) {
        std::cerr << "logp: WARNING: can't lookup host" << std::endl;
        goto bail;
    }

    fd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd == -1) {
        std::cerr << "logp: WARNING: can't create socket" << std::endl;
        goto bail;
    }

    rc = ::connect(fd, res->ai_addr, res->ai_addrlen);
    if (rc) {
        std::cerr << "logp: WARNING: can't connect" << std::endl;
        goto bail;
    }

    connection_fd = fd;
    return;

    bail:

    if (res) freeaddrinfo(res);
    if (fd != -1) close(fd);
}


}}
