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

#include "logp/cmd/run.h"
#include "logp/websocket.h"
#include "logp/signalwatcher.h"
#include "logp/preloadwatcher.h"
#include "logp/pipecapturer.h"
#include "logp/event.h"
#include "logp/util.h"


namespace logp { namespace cmd {


const char *run::usage() {
    static const char *u =
        "logp run [options] <command>\n"
        "  --no-stderr    Do not collect stderr output\n"
        "  --stdout       Collect stdout output\n"
        "  -f / --fork    Follow forked processes\n"
        "\n"
        "  <command>   This is a unix command, possibly including options\n"
    ;

    return u;
}

const char *run::getopt_string() { return "f"; }

struct option *run::get_long_options() {
    static struct option opts[] = {
        {"no-stderr", no_argument, 0, 0},
        {"stdout", no_argument, 0, 0},
        {"fork", no_argument, 0, 'f'},
        {0, 0, 0, 0}
    };

    return opts;
}

void run::process_option(int arg, int option_index, char *) {
    switch (arg) {
      case 0:
        if (strcmp(my_long_options[option_index].name, "no-stderr") == 0) {
            opt_stderr = false;
        } else if (strcmp(my_long_options[option_index].name, "stdout") == 0) {
            opt_stdout = true;
        }
        break;

      case 'f':
        opt_follow_fork = true;
        break;
    };
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

struct run_msg_proc_started {
    uint64_t timestamp;
    nlohmann::json data;
};

struct run_msg_proc_exited {
    uint64_t timestamp;
    nlohmann::json data;
};

using run_msg = mapbox::util::variant<run_msg_sigchld, run_msg_websocket_flushed, run_msg_pipe_data, run_msg_proc_started, run_msg_proc_exited>;


void run::execute() {
    if (!my_argv[optind]) {
        PRINT_ERROR << "Must provide a command after run, ie 'logp run sleep 10'";
        print_usage_and_exit();
    }


    hoytech::protected_queue<run_msg> cmd_run_queue;


    struct sigaction sa;
    sa.sa_handler = [](int){};
    sa.sa_flags = SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, nullptr);

    logp::signal_watcher sigwatcher;

    sigwatcher.subscribe(SIGCHLD, [&](){
        run_msg_sigchld m;
        cmd_run_queue.push_move(m);
    });

    logp::preload_watcher preloadwatcher;

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




    preloadwatcher.on_proc_start = [&](uint64_t ts, nlohmann::json &data){
        run_msg_proc_started m{ts, std::move(data)};
        cmd_run_queue.push_move(m);
    };

    preloadwatcher.on_proc_end = [&](uint64_t ts, nlohmann::json &data){
        run_msg_proc_exited m{ts, std::move(data)};
        cmd_run_queue.push_move(m);
    };




    sigwatcher.run();
    timer.run();
    if (opt_follow_fork) preloadwatcher.run();


    logp::websocket::worker ws_worker;

    ws_worker.run();



    std::unique_ptr<logp::pipe_capturer> stderr_pipe_capturer;
    bool stderr_finished = false;

    if (opt_stderr) {
        int fd = 2;

        stderr_pipe_capturer = std::unique_ptr<logp::pipe_capturer>(new logp::pipe_capturer(fd, timer,
            [&, fd](std::string &buf, uint64_t timestamp){
                run_msg_pipe_data m;
                m.fd = fd;
                m.timestamp = timestamp;
                m.data = std::move(buf);
                cmd_run_queue.push_move(m);
            },
            [&, fd](){
                run_msg_pipe_data m;
                m.fd = fd;
                m.finished = true;
                cmd_run_queue.push_move(m);
            }
        ));
    }

    std::unique_ptr<logp::pipe_capturer> stdout_pipe_capturer;
    bool stdout_finished = false;

    if (opt_stdout) {
        int fd = 1;

        stdout_pipe_capturer = std::unique_ptr<logp::pipe_capturer>(new logp::pipe_capturer(fd, timer,
            [&, fd](std::string &buf, uint64_t timestamp){
                run_msg_pipe_data m;
                m.fd = fd;
                m.timestamp = timestamp;
                m.data = std::move(buf);
                cmd_run_queue.push_move(m);
            },
            [&, fd](){
                run_msg_pipe_data m;
                m.fd = fd;
                m.finished = true;
                cmd_run_queue.push_move(m);
            }
        ));
    }


    uint64_t start_timestamp = logp::util::curr_time();

    pid_t ppid = getppid();

    pid_t fork_ret = fork();

    if (fork_ret == -1) {
        PRINT_ERROR << "unable to fork: " << strerror(errno);
        _exit(1);
    } else if (fork_ret == 0) {
        if (stderr_pipe_capturer) stderr_pipe_capturer->child();
        if (stdout_pipe_capturer) stdout_pipe_capturer->child();

        if (opt_follow_fork) {
            ::setenv("LOGP_SOCKET_PATH", preloadwatcher.get_socket_path().c_str(), 0);
            ::setenv("LD_PRELOAD", "./logp_preload.so", 0);
        }

        sigwatcher.unblock();
        execvp(my_argv[optind], my_argv+optind);
        PRINT_ERROR << "Couldn't exec " << my_argv[optind] << " : " << strerror(errno);
        _exit(1);
    }

    if (stderr_pipe_capturer) stderr_pipe_capturer->parent();
    if (stdout_pipe_capturer) stdout_pipe_capturer->parent();


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

            auto vars_to_capture = conf.get_strvec("run.env");

            if (vars_to_capture.size()) {
                for (char **envp = environ; *envp; envp++) {
                    std::string env_kv = std::string(*envp);

                    auto equal_sign_pos = env_kv.find_first_of('=');
                    if (equal_sign_pos == std::string::npos) continue;

                    std::string env_k = env_kv.substr(0, equal_sign_pos);

                    bool match = false;

                    for (auto v : vars_to_capture) {
                        if (::fnmatch(v.c_str(), env_k.c_str(), 0) == 0) {
                            match = true;
                            break;
                        }
                    }

                    if (match) data["env"][env_k] = env_kv.substr(equal_sign_pos+1);
                }
            }
        }

        {
            nlohmann::json body = {{ "ty", "cmd" }, { "st", start_timestamp }, { "da", data }, { "hb", conf.get_uint64("run.heartbeat", 5000000) }};
            curr_event.start(body);
        }
    }



    bool pid_exited = false;
    uint64_t end_timestamp = 0;
    bool sent_end_message = false;
    int wait_status = 0;
    struct rusage resource_usage = {};

    uint64_t next_evpid = 1;
    std::unordered_map<int, uint64_t> pid_to_evpid;

    while (1) {
        auto mv = cmd_run_queue.shift();

        mv.match([&](run_msg_sigchld &){
            uint64_t now = logp::util::curr_time();

            int my_status;
            pid_t wait_ret = wait4(fork_ret, &my_status, WNOHANG, &resource_usage);
            if (wait_ret <= 0) {
                PRINT_WARNING << "Received error from wait4: " << strerror(errno);
                return;
            }

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
            } else {
                PRINT_WARNING << "Received success from wait4 for a different process: " << wait_ret;
            }
        },
        [&](run_msg_websocket_flushed &){
            exit(WEXITSTATUS(wait_status));
        },
        [&](run_msg_pipe_data &m){
            if (m.fd == 2 && stderr_pipe_capturer) {
                if (m.data.size()) {
                    nlohmann::json body = {{ "ty", "stderr" }, { "at", m.timestamp }, { "da", { { "txt", m.data } } }};
                    curr_event.add(body);
                }

                if (m.finished) {
                    stderr_finished = true;
                }
            } else if (m.fd == 1 && stdout_pipe_capturer) {
                if (m.data.size()) {
                    nlohmann::json body = {{ "ty", "stdout" }, { "at", m.timestamp }, { "da", { { "txt", m.data } } }};
                    curr_event.add(body);
                }

                if (m.finished) {
                    stdout_finished = true;
                }
            }
        },
        [&](run_msg_proc_started &m){
            auto evpid = next_evpid++;
            pid_to_evpid[m.data["pid"]] = evpid;
            m.data["evpid"] = evpid;
            if (pid_to_evpid.count(m.data["ppid"])) {
                m.data["evppid"] = pid_to_evpid[m.data["ppid"]];
            }
            m.data["what"] = "start";
            nlohmann::json body = {{ "ty", "proc" }, { "at", m.timestamp }, { "da", m.data }};
            curr_event.add(body);
        },
        [&](run_msg_proc_exited &m){
            if (pid_to_evpid.count(m.data["pid"])) {
                m.data["evpid"] = pid_to_evpid[m.data["pid"]];
            }
            m.data["what"] = "end";
            nlohmann::json body = {{ "ty", "proc" }, { "at", m.timestamp }, { "da", m.data }};
            curr_event.add(body);
        }
        );

        if (pid_exited && (!stderr_pipe_capturer || stderr_finished) && (!stdout_pipe_capturer || stdout_finished) && !sent_end_message) {
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
