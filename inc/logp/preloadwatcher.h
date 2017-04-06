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


class preload_watcher;

class preload_connection {
  public:
    preload_connection(preload_watcher *parent_, ev::dynamic_loop &loop, int fd_) : parent(parent_), io(loop), fd(fd_) {
        io.set<preload_connection, &preload_connection::readable>(this);
        io.start(fd, ev::READ);
    }

    ~preload_connection() {
        io.stop();
        close(fd);
    }

  private:
    void readable();

    preload_watcher *parent;
    ev::io io;
    int fd;
    std::string buffer;
    int pid = -1;
    uint64_t start_ts = 0;
};

class preload_watcher {
  public:
    preload_watcher() {};
    void run();
    std::string get_socket_path() {
        return socket_path;
    }
    std::function<void(uint64_t ts, nlohmann::json &data)> on_proc_start;
    std::function<void(uint64_t ts, nlohmann::json &data)> on_proc_end;

  private:
    friend class preload_connection;

    void handle_accept(ev::io &watcher, int revents);

    std::string temp_dir;
    std::string socket_path;
    int fd = -1;
    std::thread t;
    std::unique_ptr<ev::dynamic_loop> loop;
    std::unordered_map<int, preload_connection> conn_map;
};

}
