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


class trace_engine;

class trace_engine_connection {
  public:
    trace_engine_connection(trace_engine *parent_, ev::dynamic_loop &loop, int fd_, uint64_t trace_conn_id_) : parent(parent_), input_watcher(loop), output_watcher(loop), fd(fd_), trace_conn_id(trace_conn_id_) {
        input_watcher.set<trace_engine_connection, &trace_engine_connection::try_read>(this);
        input_watcher.start(fd, ev::READ);

        output_watcher.set<trace_engine_connection, &trace_engine_connection::try_write>(this);
    }

    ~trace_engine_connection() {
        input_watcher.stop();
        output_watcher.stop();
        close(fd);
    }

    void send(const char *buf, size_t len) {
        output_buffer.append(buf, len);
        try_write();
    }

  private:
    friend class trace_engine;

    void try_read();
    void try_write();

    trace_engine *parent;
    ev::io input_watcher;
    ev::io output_watcher;
    int fd;
    std::string input_buffer;
    std::string output_buffer;
    uint64_t trace_conn_id;
    bool initialized = false;
};

class trace_engine {
  public:
    trace_engine() {};
    void run();
    std::string get_socket_path() {
        return socket_path;
    }
    std::function<void(uint64_t ts, nlohmann::json &data)> on_event;

  private:
    friend class trace_engine_connection;

    void handle_accept(ev::io &watcher, int revents);
    void send_to_conn(uint64_t trace_conn_id, const char *msg, size_t len);

    void on_new_conn(trace_engine_connection &conn, nlohmann::json &j);
    void on_data(trace_engine_connection &conn, nlohmann::json &j);
    void on_close_conn(trace_engine_connection &conn);

    std::string temp_dir;
    std::string socket_path;
    int fd = -1;
    std::thread t;
    std::unique_ptr<ev::dynamic_loop> loop;
    std::unordered_map<uint64_t, trace_engine_connection> conn_map;
    uint64_t next_trace_conn_id = 1;

    bool listening = false;
    std::vector<uint64_t> conns_pending_listen;
};

}
