#include <string.h>
#include <errno.h>
#include <signal.h>

#include "logp/util.h"
#include "logp/signalwatcher.h"


namespace logp {


signal_watcher::signal_watcher() {
    sigemptyset(&set);
}


void signal_watcher::subscribe(int signum, std::function<void()> cb) {
    cbs[signum] = cb;
    sigaddset(&set, signum);
}

void signal_watcher::ignore(int signum) {
    subscribe(signum, [](){});
}

void signal_watcher::unblock() {
    pthread_sigmask(SIG_UNBLOCK, &set, NULL);
}


void signal_watcher::run() {
    int s = pthread_sigmask(SIG_BLOCK, &set, NULL);
    if (s != 0) {
        throw logp::error("unable to call pthread_sigmask: ", strerror(errno));
    }

    t = std::thread([this]() {
        while (1) {
            int signum;
            int ret = sigwait(&set, &signum);
            if (ret != 0) {
                throw logp::error("failure calling sigwait: ", strerror(errno));
            }

            auto &cb = cbs.at(signum);

            cb();
        }
    });
}
    

}
