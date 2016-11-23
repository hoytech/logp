#include <unistd.h> // FIXME

#include <iostream> // FIXME

#include "protected_queue/protected_queue.h"

#include "logp/messages.h"
#include "logp/websocket.h"


int main() {
    //hoytech::protected_queue<std::string> q;

    std::string uri("ws://localhost:8001");

    logp::websocket::connection c(uri);
    c.start();

    sleep(100);

    return 0;
}
