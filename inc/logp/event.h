#pragma once

#include <string.h>

#include <string>
#include <map>
#include <functional>
#include <mutex>

#include "hoytech/timer.h"
#include "nlohmann/json.hpp"

#include "logp/util.h"
#include "logp/websocket.h"


namespace logp {

class event {
  public:
    event(hoytech::timer &timer_, logp::websocket::worker &ws_worker_) : timer(timer_), ws_worker(ws_worker_) {}

    void start(nlohmann::json &body);
    void add(nlohmann::json &body);
    void end(nlohmann::json &end);

    std::function<void()> on_flushed;
  private:
    void handle_start_ack(nlohmann::json &resp);
    void attempt_to_send(uint64_t internal_entry_id);
    void attempt_to_send_all_pending();

    hoytech::timer &timer;
    logp::websocket::worker &ws_worker;

    std::mutex internal_mutex;

    uint64_t next_internal_entry_id = 1;
    std::map<uint64_t, nlohmann::json> pending_queue;
    std::map<uint64_t, nlohmann::json> in_flight_queue;

    int heartbeat_interval = 0;
    hoytech::timer::cancel_token heartbeat_timer_cancel_token = 0;
    uint64_t event_id = 0;
    bool started = false;
    bool ended = false;
};

}
