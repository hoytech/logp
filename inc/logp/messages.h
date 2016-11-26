#pragma once

#include <string>


namespace logp { namespace msg {


class websocket_input {
  public:
    websocket_input(std::string data_) : data(data_) {}

    // Input

    std::string data;
};

class websocket_output {
  public:
    websocket_output(std::string data_) : data(data_) {}

    // Input

    std::string data;
};



enum class cmd_run_msg_type { PROCESS_EXITED, WEBSOCKET_RESPONSE };

class cmd_run {
  public:
    cmd_run(cmd_run_msg_type type_) : type(type_) {}

    cmd_run_msg_type type;

    // PROCESS_EXITED
    int pid = 0;
    int wait_status = 0;
    uint64_t timestamp = 0;

    // WEBSOCKET_RESPONSE
    std::string response;
};



}}
