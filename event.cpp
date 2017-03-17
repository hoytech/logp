#include <stdexcept>

#include "logp/event.h"
#include "logp/util.h"
#include "logp/websocket.h"


namespace logp {

void event::start(nlohmann::json &body) {
    {
        std::unique_lock<std::mutex> lock(internal_mutex);

        if (started) throw logp::error("event already started");
        started = true;

        if (!body.count("st")) {
            throw logp::error("first message in an event must have 'st' param");
        }

        if (body.count("hb")) heartbeat_interval = body["hb"];
    }

    add(body);
}


void event::end(nlohmann::json &body) {
    {
        std::unique_lock<std::mutex> lock(internal_mutex);

        if (!started) throw logp::error("event hasn't been started");

        if (ended) throw logp::error("event has already been ended");
        ended = true;

        if (!body.count("en")) {
            throw logp::error("last message in an event must have 'en' param");
        }
    }

    add(body);
}


void event::add(nlohmann::json &body) {
    std::unique_lock<std::mutex> lock(internal_mutex);

    uint64_t internal_entry_id = next_internal_entry_id++;

    if (event_id && !body.count("ev")) {
        body["ev"] = event_id;
    }

    internal_queue.emplace(internal_entry_id, std::move(body));

    attempt_to_send();
}



// Must have lock on internal_mutex while calling
void event::attempt_to_send() {
    for (auto &pair : internal_queue) {
        uint64_t internal_entry_id = pair.first;
        auto &body = pair.second;

        if (internal_entry_id != 1) {
            if (!event_id) continue;
            if (!body.count("ev")) body["ev"] = event_id;
        }

        logp::websocket::request r;

        r.op = "add";
        r.body = body;
        r.on_data = [&, internal_entry_id](nlohmann::json &resp){
            std::unique_lock<std::mutex> lock(internal_mutex);
            internal_queue.erase(internal_entry_id);
            if (internal_entry_id == 1 && !event_id) handle_start_ack(resp);
            if (ended && internal_queue.size() == 0) {
                lock.unlock();
                if (on_flushed) on_flushed();
            }
        };

        ws_worker.push_move_new_request(r);
    }
}


// Must have lock on internal_mutex while calling
void event::handle_start_ack(nlohmann::json &resp) {
    if (!resp.count("ev")) {
        PRINT_ERROR << "response start event didn't have event_id?";
        return;
    }

    event_id = resp["ev"];
    PRINT_DEBUG << "assigned event_id: " << event_id;

    if (heartbeat_interval) {
        heartbeat_timer_cancel_token = timer.repeat(heartbeat_interval, [&]{
            logp::websocket::request r;

            r.op = "hrt";
            r.body = {{ "ev", event_id }};

            ws_worker.push_move_new_request(r);
        });
    }

    attempt_to_send();
}

}
