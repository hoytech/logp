#include <unistd.h> // FIXME

#include <iostream> // FIXME

#include "logp/messages.h"
#include "logp/websocket.h"

#include "nlohmann/json.hpp"


int main() {
    //std::string uri("ws://localhost:8001");
    std::string uri("ws://localhost:8000/ws/");

    logp::websocket::worker c(uri);
    c.run();

    {
        nlohmann::json j = {{ "id", 123 }, { "op", "get" }};
        std::string data = j.dump();
        data += "\n{}";
        logp::msg::websocket_input m(std::move(data));
        c.input_queue.push_move(m);
        c.trigger_input_queue();
    }

    sleep(100);

    return 0;
}
