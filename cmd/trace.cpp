#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pwd.h>
#include <fnmatch.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/resource.h>

#include <iostream>
#include <string>
#include <fstream>
#include <memory>
#include <thread>
#include <unordered_map>

#include "mapbox/variant.hpp"
#include "nlohmann/json.hpp"
#include "hoytech/timer.h"

#include "logp/cmd/trace.h"
#include "logp/signalwatcher.h"
#include "logp/traceengine.h"
#include "logp/pipecapturer.h"
#include "logp/event.h"
#include "logp/util.h"


namespace logp { namespace cmd {


const char *trace::usage() {
    static const char *u =
        "logp trace [options] <command>\n"
        "\n"
        "  <command>   This is a unix command, possibly including options\n"
    ;

    return u;
}

const char *trace::getopt_string() { return ""; }

struct option *trace::get_long_options() {
    static struct option opts[] = {
        {0, 0, 0, 0}
    };

    return opts;
}

void trace::process_option(int arg, int, char *) {
    switch (arg) {
      case 0:
        break;
    };
}



std::string find_logp_trace_preload() {
    std::string path;

    path = "/usr/logp/lib/logp_trace_preload.so";
    if (access(path.c_str(), R_OK) == 0) return path;

    path = "/usr/local/logp/lib/logp_trace_preload.so";
    if (access(path.c_str(), R_OK) == 0) return path;

    char cwd[1024];
    if (!getcwd(cwd, sizeof(cwd))) {
      PRINT_WARNING << "unable to getcwd(): " << strerror(errno);
      return "";
    }

    path = cwd;
    path += "/logp_trace_preload.so";
    if (access(path.c_str(), R_OK) == 0) return path;

    PRINT_WARNING << "unable to find logp_trace_preload.so";
    return "";
}



struct trace_msg_sigchld {
};

using trace_msg = mapbox::util::variant<trace_msg_sigchld>;


void trace::execute() {
    if (!my_argv[optind]) {
        PRINT_ERROR << "Must provide a command after trace, ie 'logp trace sleep 10'";
        print_usage_and_exit();
    }


    hoytech::protected_queue<trace_msg> cmd_trace_queue;


    struct sigaction sa;
    sa.sa_handler = [](int){};
    sa.sa_flags = SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, nullptr);

    logp::signal_watcher sigwatcher;

    sigwatcher.subscribe(SIGCHLD, [&](){
        trace_msg_sigchld m;
        cmd_trace_queue.push_move(m);
    });

    logp::trace_engine tracer;



    sigwatcher.run();
    tracer.run();


    std::string logp_preload_path = find_logp_trace_preload();
    if (!logp_preload_path.size()) {
        PRINT_ERROR << "unable to find preload library";
        exit(1);
    }


    pid_t fork_ret = fork();

    if (fork_ret == -1) {
        PRINT_ERROR << "unable to fork: " << strerror(errno);
        _exit(1);
    } else if (fork_ret == 0) {
        ::setenv("LOGP_SOCKET_PATH", tracer.get_socket_path().c_str(), 0);
        ::setenv("LD_PRELOAD", logp_preload_path.c_str(), 0);

        sigwatcher.unblock();
        execvp(my_argv[optind], my_argv+optind);
        PRINT_ERROR << "Couldn't exec " << my_argv[optind] << " : " << strerror(errno);
        _exit(1);
    }


    PRINT_INFO << "Executing " << my_argv[optind] << " (pid " << fork_ret << ")";


    while (1) {
        auto mv = cmd_trace_queue.shift();

        mv.match([&](trace_msg_sigchld &){
            int wait_status;
            struct rusage resource_usage = {};
            pid_t wait_ret = wait4(fork_ret, &wait_status, WNOHANG, &resource_usage);
            if (wait_ret <= 0) {
                PRINT_WARNING << "Received error from wait4: " << strerror(errno);
                return;
            }

            if (wait_ret == fork_ret) {
                PRINT_INFO << "Process exited (" <<
                    (WIFEXITED(wait_status) ? (std::string("status ") + std::to_string(WEXITSTATUS(wait_status)))
                     : WIFSIGNALED(wait_status) ? (std::string("signal ") + std::to_string(WTERMSIG(wait_status)))
                     : "other")
                    << ")"
                ;

                exit(WEXITSTATUS(wait_status));
            } else {
                PRINT_WARNING << "Received success from wait4 for a different process: " << wait_ret;
            }
        }
        );

    }
}

}}
