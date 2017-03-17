#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/resource.h>

#include <iostream>
#include <string>
#include <fstream>
#include <memory>
#include <thread>

#include "mapbox/variant.hpp"
#include "nlohmann/json.hpp"
#include "hoytech/timer.h"

#include "logp/cmd/run.h"
#include "logp/websocket.h"
#include "logp/signalwatcher.h"
#include "logp/pipecapturer.h"
#include "logp/event.h"
#include "logp/util.h"


namespace logp { namespace cmd {


const char *run::usage() {
    static const char *u =
        "logp run [options] <command>\n"
        "  --parent-cmd    Collect the name of the parent process\n"
        "\n"
        "  <command>   This is a unix command, possibly including options\n"
    ;

    return u;
}

const char *run::getopt_string() { return ""; }

struct option *run::get_long_options() {
    static struct option opts[] = {
        {"parent-cmd", no_argument, 0, 0},
        {0, 0, 0, 0}
    };

    return opts;
}

void run::process_option(int, int, char *) {
}



struct run_msg_sigchld {
};

struct run_msg_websocket_flushed {
};

struct run_msg_pipe_data {
    int fd = -1;
    bool finished = false;
    uint64_t timestamp = 0;
    std::string data;
};

using run_msg = mapbox::util::variant<run_msg_sigchld, run_msg_websocket_flushed, run_msg_pipe_data>;


void run::execute() {
    if (!my_argv[optind]) {
        PRINT_ERROR << "Must provide a command after run, ie 'logp run sleep 10'";
        print_usage_and_exit();
    }


    hoytech::protected_queue<run_msg> cmd_run_queue;


    signal(SIGCHLD, [](int){});

    logp::signal_watcher sigwatcher;

    sigwatcher.subscribe(SIGCHLD, [&](){
        run_msg_sigchld m;
        cmd_run_queue.push_move(m);
    });

    hoytech::timer timer;

    bool kill_timeout_normal_shutdown = false;
    bool kill_timeout_timer_started = false;

    auto kill_signal_handler = [&](){
        if (kill_timeout_timer_started) return;
        kill_timeout_timer_started = true;

        if (!kill_timeout_normal_shutdown) PRINT_WARNING << "attempting to communicate with log periodic server, please wait...";

        timer.once(4*1000000, []{
            PRINT_ERROR << "was unable to communicate with log periodic server";
            exit(1);
        });
    };

    sigwatcher.subscribe(SIGHUP, kill_signal_handler);
    sigwatcher.subscribe(SIGINT, kill_signal_handler);
    sigwatcher.subscribe(SIGQUIT, kill_signal_handler);
    sigwatcher.subscribe(SIGTERM, kill_signal_handler);

    sigwatcher.run();

    timer.run();


    logp::websocket::worker ws_worker;

    ws_worker.run();



    std::unique_ptr<logp::pipe_capturer> stderr_pipe_capturer;
    bool stderr_finished = false;

    stderr_pipe_capturer = std::unique_ptr<logp::pipe_capturer>(new logp::pipe_capturer(2, timer,
        [&](std::string &buf, uint64_t timestamp){
            run_msg_pipe_data m;
            m.fd = 2;
            m.timestamp = timestamp;
            m.data = std::move(buf);
            cmd_run_queue.push_move(m);
        },
        [&](){
            run_msg_pipe_data m;
            m.fd = 2;
            m.finished = true;
            cmd_run_queue.push_move(m);
        }
    ));



    uint64_t start_timestamp = logp::util::curr_time();

    pid_t ppid = getppid();

    pid_t fork_ret = fork();

    if (fork_ret == -1) {
        PRINT_ERROR << "unable to fork: " << strerror(errno);
        _exit(1);
    } else if (fork_ret == 0) {
        if (stderr_pipe_capturer) stderr_pipe_capturer->child();

        sigwatcher.unblock();
        execvp(my_argv[optind], my_argv+optind);
        PRINT_ERROR << "Couldn't exec " << my_argv[optind] << " : " << strerror(errno);
        _exit(1);
    }

    if (stderr_pipe_capturer) stderr_pipe_capturer->parent();


    logp::event curr_event(timer, ws_worker);

    curr_event.on_flushed = [&](){
        run_msg_websocket_flushed m;
        cmd_run_queue.push_move(m);
    };

    PRINT_INFO << "Executing " << my_argv[optind] << " (pid " << fork_ret << ")";


    {
        nlohmann::json data;

        {
            data["type"] = "cmd";

            for (char **a = my_argv+optind; *a; a++) data["cmd"].push_back(*a);

            char hostname[256];
            if (!gethostname(hostname, sizeof(hostname))) {
                data["hostname"] = hostname;
            } else {
                PRINT_ERROR << "Couldn't determine hostname: " << strerror(errno);
            }

            struct passwd *pw = getpwuid(geteuid());

            if (pw) data["user"] = pw->pw_name;

            data["pid"] = fork_ret;
            data["ppid"] = ppid;
        }

        {
            nlohmann::json body = {{ "ty", "cmd" }, { "st", start_timestamp }, { "da", data }, { "hb", ::conf.heartbeat_interval }};
            curr_event.start(body);
        }
    }



    bool pid_exited = false;
    uint64_t end_timestamp = 0;
    bool sent_end_message = false;
    int wait_status = 0;
    struct rusage resource_usage = {};

    while (1) {
        auto mv = cmd_run_queue.pop();

        mv.match([&](run_msg_sigchld &){
            uint64_t now = logp::util::curr_time();

            int my_status;
            pid_t wait_ret = wait4(-1, &my_status, WNOHANG, &resource_usage);
            if (wait_ret <= 0) return;

            if (wait_ret == fork_ret) {
                end_timestamp = now;
                wait_status = my_status;
                pid_exited = true;

                PRINT_INFO << "Process exited (" <<
                    (WIFEXITED(wait_status) ? (std::string("status ") + std::to_string(WEXITSTATUS(wait_status)))
                     : WIFSIGNALED(wait_status) ? (std::string("signal ") + std::to_string(WTERMSIG(wait_status)))
                     : "other")
                    << ")"
                ;

                kill_timeout_normal_shutdown = true;
                kill_signal_handler();
            }
        },
        [&](run_msg_websocket_flushed &){
            exit(WEXITSTATUS(wait_status));
        },
        [&](run_msg_pipe_data &m){
            if (m.fd == 2 && stderr_pipe_capturer) {
                if (m.data.size()) {
//std::cerr << "STDERR: [" << m.data << "]" << std::endl;
                }

                if (m.finished) {
                    stderr_finished = true;
                }
            }
        });

        if (pid_exited && (!stderr_pipe_capturer || stderr_finished) && !sent_end_message) {
            nlohmann::json data;

            if (WIFEXITED(wait_status)) {
                data["term"] = "exit";
                data["exit"] = WEXITSTATUS(wait_status);
            } else if (WIFSIGNALED(wait_status)) {
                data["term"] = "signal";
                data["signal"] = strsignal(WTERMSIG(wait_status));
                if (WCOREDUMP(wait_status)) data["core"] = true;
            } else {
                data["term"] = "unknown";
            }

            long ru_maxrss = resource_usage.ru_maxrss;

#ifdef __APPLE__
            ru_maxrss /= 1024; // In bytes on OS X
#endif

            data["rusage"]["utime"] = logp::util::timeval_to_usecs(resource_usage.ru_utime);
            data["rusage"]["stime"] = logp::util::timeval_to_usecs(resource_usage.ru_stime);
            data["rusage"]["maxrss"] = ru_maxrss;
            data["rusage"]["minflt"] = resource_usage.ru_minflt;
            data["rusage"]["majflt"] = resource_usage.ru_majflt;
            data["rusage"]["inblock"] = resource_usage.ru_inblock;
            data["rusage"]["oublock"] = resource_usage.ru_oublock;
            data["rusage"]["nvcsw"] = resource_usage.ru_nvcsw;
            data["rusage"]["nivcsw"] = resource_usage.ru_nivcsw;

            nlohmann::json body = {{ "ty", "cmd" }, { "en", end_timestamp }, { "da", data }};
            curr_event.end(body);

            sent_end_message = true;
        }
    }
}

}}
