#pragma once

#include <signal.h>

#include <functional>
#include <thread>
#include <unordered_map>


namespace logp {

class signal_watcher {
  public:
    signal_watcher();
    void subscribe(int signum, std::function<void()> cb);
    void run();

  private:
    sigset_t set;
    std::thread t;
    std::unordered_map<int, std::function<void()>> cbs;
};

}
