#include <iostream>

#include "protected_queue/protected_queue.h"

#include "logp/messages.h"
#include "logp/websocket.h"


int main() {
    hoytech::protected_queue<std::string> q;

    websocket::connection c("ws://localhost:8001", &q);

    return 0;
}
