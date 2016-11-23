#include "logp/websocket.h"

namespace logp { namespace websocket {

connection::connection(std::string &uri) {
  t = std::thread([this]() {
  });
}


}}
