#pragma once

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



}}
