#pragma once

#include <unistd.h>

#include <functional>
#include <thread>
#include <unordered_map>
#include <string>
#include <memory>

#include "nlohmann/json.hpp"
#include "ev++.h"

#include "logp/util.h"


namespace logp {


class preload_watcher2;

class preload_connection2 {
  public:
    preload_connection2(preload_watcher2 *parent_, ev::dynamic_loop &loop, int fd_, uint64_t trace_conn_) : parent(parent_), input_watcher(loop), output_watcher(loop), fd(fd_), trace_conn(trace_conn_) {
        input_watcher.set<preload_connection2, &preload_connection2::try_read>(this);
        input_watcher.start(fd, ev::READ);

        output_watcher.set<preload_connection2, &preload_connection2::try_write>(this);
    }

    void send(const char *buf, size_t len) {
        output_buffer.append(buf, len);
        try_write();
    }

    ~preload_connection2() {
        input_watcher.stop();
        output_watcher.stop();
        close(fd);
    }

  private:
    void try_read();
    void try_write();

    preload_watcher2 *parent;
    ev::io input_watcher;
    ev::io output_watcher;
    int fd;
    std::string input_buffer;
    std::string output_buffer;
    uint64_t trace_conn;
};

class preload_watcher2 {
  public:
    preload_watcher2() {};
    void run();
    std::string get_socket_path() {
        return socket_path;
    }
    std::function<void(uint64_t ts, nlohmann::json &data)> on_event;

  private:
    friend class preload_connection2;

    void handle_accept(ev::io &watcher, int revents);

    std::string temp_dir;
    std::string socket_path;
    int fd = -1;
    std::thread t;
    std::unique_ptr<ev::dynamic_loop> loop;
    std::unordered_map<int, preload_connection2> conn_map;
    uint64_t next_trace_conn = 1;
};

}
