#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <iostream>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "logp/util.h"
#include "logp/preloadwatcher.h"


namespace logp {

bool atexit_handler_registered = false;
std::vector<std::string> tmpdirs_to_cleanup;

preload_watcher::preload_watcher() {
    char my_temp_dir[] = "/tmp/logp-XXXXXX";
    if (!mkdtemp(my_temp_dir)) throw logp::error("mkdtemp error: ", strerror(errno));

    temp_dir = std::string(my_temp_dir);
    socket_path = temp_dir + "/logp.socket";

    tmpdirs_to_cleanup.push_back(temp_dir);

    if (!atexit_handler_registered) {
        atexit_handler_registered = true;

        ::atexit([](){
            for(auto &dir : tmpdirs_to_cleanup) {
                std::string socket = dir + "/logp.socket";
                unlink(socket.c_str());
                rmdir(dir.c_str());
            }
        });
    }

    struct sockaddr_un un;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd == -1) throw logp::error("unable to create unix socket: ", strerror(errno));

    memset(&un, 0, sizeof(struct sockaddr_un));
    un.sun_family = AF_UNIX;
    strncpy(un.sun_path, socket_path.c_str(), sizeof(un.sun_path) - 1);

    if (bind(fd, reinterpret_cast<const sockaddr*>(&un), sizeof(un)) == -1) {
        throw logp::error("unable to bind unix socket to '", socket_path, "': ", strerror(errno));
    }

    if (listen(fd, 5) == -1) {
        throw logp::error("unable to listen on unix socket '", socket_path, "': ", strerror(errno));
    }
}

void preload_watcher::run() {
    t = std::thread([this]() {
        //ev::dynamic_loop loop(EVFLAG_NOSIGMASK);
        loop = std::unique_ptr<ev::dynamic_loop>(new ev::dynamic_loop(EVFLAG_NOSIGMASK));

        ev::io accept_io(*loop);

        accept_io.set<preload_watcher, &preload_watcher::handle_accept>(this);
        accept_io.start(fd, ev::READ);

        loop->run();
    });
}

void preload_watcher::handle_accept(ev::io &, int) {
    int conn_fd = ::accept(fd, nullptr, nullptr);

    if (conn_fd < 0) return;

    conn_map.emplace(std::piecewise_construct,
                     std::forward_as_tuple(conn_fd),
                     std::forward_as_tuple(this, *loop, conn_fd));
}


void preload_connection::readable() {
    char tmpbuf[4096];
    auto ret = read(fd, tmpbuf, sizeof(tmpbuf));

    if (ret <= 0) {
        auto j = nlohmann::json({
            {"msg", "exit"},
            {"pid", pid}
        });
        std::cerr << "PRELOAD EXIT: " << j.dump() << std::endl;

        parent->conn_map.erase(fd);
        return;
    }

    buffer += std::string(tmpbuf, (size_t)ret);

    if (buffer.size() && buffer[buffer.size() - 1] == '\n') {
        // assumes client only sends 1 line of data
        buffer.resize(buffer.size() - 1);

        auto j = nlohmann::json::parse(buffer);
        j["msg"] = "create";
        std::cerr << "PRELOAD CREATE: " << j.dump() << std::endl;

        if (j.count("pid")) pid = j["pid"];
    }
}

}
