#include <string.h>
#include <errno.h>
#include <signal.h>

#include <stdexcept>

#include "logp/signalwatcher.h"


namespace logp {


signal_watcher::signal_watcher() {
    sigemptyset(&set);
}


void signal_watcher::subscribe(int signum, std::function<void()> cb) {
    cbs[signum] = cb;
    sigaddset(&set, signum);
}


void signal_watcher::run() {
    int s = pthread_sigmask(SIG_BLOCK, &set, NULL);
    if (s != 0) {
        throw std::runtime_error(std::string("unable to call pthread_sigmask: ") + strerror(errno));
    }

    t = std::thread([this]() {
        while (1) {
            int signum;
            int ret = sigwait(&set, &signum);
            if (ret != 0) {
                throw std::runtime_error(std::string("failure calling sigwait: ") + strerror(errno));
            }

            auto &cb = cbs.at(signum);

            cb();
        }
    });
}
    

}
