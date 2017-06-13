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
#include "logp/traceengine.h"


namespace logp {

bool traceengine_atexit_handler_registered = false;
std::vector<std::string> traceengine_tmpdirs_to_cleanup;

void trace_engine::run() {
    char my_temp_dir[] = "/tmp/logp-XXXXXX";
    if (!mkdtemp(my_temp_dir)) throw logp::error("mkdtemp error: ", strerror(errno));

    temp_dir = std::string(my_temp_dir);
    socket_path = temp_dir + "/logp.socket";

    traceengine_tmpdirs_to_cleanup.push_back(temp_dir);

    if (!traceengine_atexit_handler_registered) {
        traceengine_atexit_handler_registered = true;

        ::atexit([](){
            for(auto &dir : traceengine_tmpdirs_to_cleanup) {
                std::string socket = dir + "/logp.socket";
                unlink(socket.c_str());
                rmdir(dir.c_str());
            }
        });
    }

    struct sockaddr_un un;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd == -1) throw logp::error("unable to create unix socket: ", strerror(errno));

    logp::util::make_fd_nonblocking(fd);

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
        loop = std::unique_ptr<ev::dynamic_loop>(new ev::dynamic_loop(EVFLAG_NOSIGMASK));

        ev::io accept_io(*loop);

        accept_io.set<trace_engine, &trace_engine::handle_accept>(this);
        accept_io.start(fd, ev::READ);

        loop->run();
    });
}

void trace_engine::handle_accept(ev::io &, int) {
    int conn_fd = ::accept(fd, nullptr, nullptr);

    if (conn_fd < 0) return;

    uint64_t trace_conn_id = next_trace_conn_id++;

    conn_map.emplace(std::piecewise_construct,
                     std::forward_as_tuple(trace_conn_id),
                     std::forward_as_tuple(this, *loop, conn_fd, trace_conn_id));
}


void trace_engine_connection::try_read() {
    char tmpbuf[4096];
    auto ret = ::read(fd, tmpbuf, sizeof(tmpbuf));

    if (ret <= 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return;

        parent->on_close_conn(trace_conn_id);
        parent->conn_map.erase(trace_conn_id);

        return;
    }

    input_buffer.append(tmpbuf, (size_t)ret);

    while (input_buffer.size()) {
        auto len = input_buffer.find('\n');
        if (len == std::string::npos) break;

        auto j = nlohmann::json::parse(input_buffer.substr(0, len));
        input_buffer.erase(0, len+1);

        if (!initialized) {
            initialized = true;
            parent->on_new_conn(trace_conn_id, j);
        } else {
            parent->on_data(trace_conn_id, j);
        }
    }
}

void trace_engine_connection::try_write() {
    if (output_buffer.size()) {
        auto ret = ::write(fd, output_buffer.data(), output_buffer.size());

        if (ret > 0) {
            output_buffer.erase(0, (size_t)ret);
        } else if (ret < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                // do nothing
            } else {
                output_watcher.stop();
                return;
            }
        }
    }

    if (output_buffer.size()) {
        output_watcher.start(fd, ev::WRITE);
    } else {
        output_watcher.stop();
    }
}



void trace_engine::on_new_conn(uint64_t trace_conn_id, nlohmann::json &j) {
    PRINT_INFO << "[" << trace_conn_id << "] NEW: " << j.dump();

    auto *runspec = conf.get_node("trace");

    std::string msg = runspec->dump();
    msg += "\n";

    conn_map.at(trace_conn_id).send(msg.data(), msg.size());
}
void trace_engine::on_data(uint64_t trace_conn_id, nlohmann::json &j) {
    PRINT_INFO << "[" << trace_conn_id << "]: " << j.dump();
}
void trace_engine::on_close_conn(uint64_t trace_conn_id) {
    PRINT_INFO << "[" << trace_conn_id << "] CLOSE";
}




}
