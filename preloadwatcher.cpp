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

#include "logp/util.h"
#include "logp/preloadwatcher.h"


namespace logp {

bool atexit_handler_registered = false;
std::vector<std::string> tmpdirs_to_cleanup;

void preload_watcher::run() {
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
        uint64_t end_ts = logp::util::curr_time();;

        auto j = nlohmann::json({
            {"pid", pid}
        });

        if (parent->on_proc_end) parent->on_proc_end(end_ts, j);

        parent->conn_map.erase(fd);
        return;
    }

    if (start_ts == 0) start_ts = logp::util::curr_time();

    buffer += std::string(tmpbuf, (size_t)ret);

    if (buffer.size() >= sizeof(size_t)) {
        size_t msg_len;
        memcpy(&msg_len, buffer.data(), sizeof(size_t));
        if (buffer.size() < msg_len) return;

        nlohmann::json j;

        std::string payload = buffer.substr(sizeof(size_t));
        size_t curr_offset = 0;

        while(1) {
            std::string type_str = payload.substr(curr_offset, sizeof(uint16_t));
            if (type_str.size() != sizeof(uint16_t)) break;
            uint16_t field_type;
            memcpy(&field_type, type_str.data(), sizeof(uint16_t));
            curr_offset += sizeof(uint16_t);

            std::string len_str = payload.substr(curr_offset, sizeof(size_t));
            if (len_str.size() != sizeof(size_t)) break;
            size_t field_len;
            memcpy(&field_len, len_str.data(), sizeof(size_t));
            curr_offset += sizeof(size_t);

            std::string field = payload.substr(curr_offset, field_len);
            if (field.size() != field_len) break;
            curr_offset += field_len;

            if (field_type == 1) {
                if (field.size() == sizeof(pid_t)) {
                    pid_t pid;
                    memcpy(&pid, field.data(), sizeof(pid_t));
                    j["pid"] = pid;
                }
            } else if (field_type == 2) {
                if (field.size() == sizeof(pid_t)) {
                    pid_t pid;
                    memcpy(&pid, field.data(), sizeof(pid_t));
                    j["ppid"] = pid;
                }
            } else if (field_type == 3) {
                size_t i = 0, pos = field.find('\0');
                while (pos != std::string::npos) {
                    j["argv"].push_back(field.substr(i, pos-i));
                    i = ++pos;
                    pos = field.find('\0', pos);

                    if (pos == std::string::npos) j["argv"].push_back(field.substr(i, field.length()));
                }
            }
        }

        if (j.count("pid")) pid = j["pid"];

        if (parent->on_proc_start) parent->on_proc_start(start_ts, j);
    }
}

}
