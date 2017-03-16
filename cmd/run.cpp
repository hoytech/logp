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

void run::process_option(int arg, int option_index, char *) {
    switch (arg) {
      case 0:
        if (strcmp(my_long_options[option_index].name, "parent-cmd") == 0) {
            opt_parent_cmd = true;
        }
        break;
    };
}



struct run_msg_process_exited {
    int pid = 0;
    int wait_status = 0;
    uint64_t timestamp = 0;
    struct rusage resource_usage = {};
};

struct run_msg_websocket_response {
    nlohmann::json response;
};

struct run_msg_pipe_data {
    int fd = -1;
    bool finished = false;
    uint64_t timestamp = 0;
    std::string data;
};

using run_msg = mapbox::util::variant<run_msg_process_exited, run_msg_websocket_response, run_msg_pipe_data>;


void run::execute() {
    if (!my_argv[optind]) {
        PRINT_ERROR << "Must provide a command after run, ie 'logp run sleep 10'";
        print_usage_and_exit();
    }


    hoytech::protected_queue<run_msg> cmd_run_queue;


    signal(SIGCHLD, [](int){});

    logp::signal_watcher sigwatcher;

    sigwatcher.subscribe(SIGCHLD, [&](){
        struct timeval tv;
        gettimeofday(&tv, nullptr);

        int status;
        struct rusage resource_usage;
        pid_t pid = wait4(-1, &status, WNOHANG, &resource_usage);
        if (pid > 0) {
            run_msg_process_exited m;
            m.pid = pid;
            m.wait_status = status;
            m.timestamp = (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
            m.resource_usage = resource_usage;
            cmd_run_queue.push_move(m);
        }
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


    struct timeval start_tv;
    gettimeofday(&start_tv, nullptr);
    uint64_t start_timestamp = (uint64_t)start_tv.tv_sec * 1000000 + start_tv.tv_usec;

    pid_t ppid = getppid();
    std::string parent_cmd;

    if (opt_parent_cmd) {
#ifdef __linux__
        try {
            std::ifstream in(std::string("/proc/") + std::to_string(ppid) + std::string("/cmdline"));
            std::getline(in, parent_cmd, '\0');
        } catch (std::exception &e) {
            PRINT_WARNING << "unable to get parent process name: " << e.what();
        }
#else
        PRINT_WARNING << "--parent-cmd is currently only supported on linux, ignoring";
#endif
    }



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
            if (parent_cmd.size()) data["parent_cmd"] = parent_cmd;
        }

        {
            logp::websocket::request r;

            r.op = "add";
            r.body = {{ "ty", "cmd" }, { "st", start_timestamp }, { "da", data }, { "hb", ::conf.heartbeat_interval }};
            r.on_data = [&](nlohmann::json &resp) {
                run_msg_websocket_response m;
                m.response = resp;
                cmd_run_queue.push_move(m);
            };

            ws_worker.push_move_new_request(r);
        }
    }



    bool pid_exited = false;
    uint64_t end_timestamp = 0;
    bool have_event_id = false;
    uint64_t event_id = 0;
    bool sent_end_message = false;
    int wait_status = 0;
    struct rusage resource_usage = {};

    timer.repeat_maybe(::conf.heartbeat_interval, [&]{
        if (pid_exited) return false;
        if (!have_event_id) return true;

        logp::websocket::request r;

        r.op = "hrt";
        r.body = {{ "ev", event_id }};

        ws_worker.push_move_new_request(r);

        return true;
    });

    while (1) {
        auto mv = cmd_run_queue.pop();

        mv.match([&](run_msg_process_exited &m){
            if (m.pid == fork_ret) {
                end_timestamp = m.timestamp;
                wait_status = m.wait_status;
                resource_usage = m.resource_usage;
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
        [&](run_msg_websocket_response &m){
            if (!sent_end_message) {
                try {
                    if (m.response["status"] == "ok") {
                        PRINT_INFO << "log periodic server acked process start";
                        event_id = m.response["ev"];
                        have_event_id = true;
                    } else {
                        PRINT_ERROR << "status was not OK on start response: " << m.response.dump();
                    }
                } catch (std::exception &e) {
                    PRINT_ERROR << "Unable to parse JSON body to extract event id: " << e.what();
                }
            } else {
                try {
                    if (m.response["status"] == "ok") {
                        PRINT_INFO << "log periodic server acked process termination";
                        exit(WEXITSTATUS(wait_status));
                    } else {
                        PRINT_ERROR << "status was not OK on end response: " << m.response.dump();
                    }
                } catch (std::exception &e) {
                    PRINT_ERROR << "Unable to parse JSON body to confirm end: " << e.what();
                }
            }
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

        if (pid_exited && have_event_id && (!stderr_pipe_capturer || stderr_finished) && !sent_end_message) {
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

            {
                logp::websocket::request r;

                r.op = "add";
                r.body = {{ "ty", "cmd" }, { "ev", event_id }, { "en", end_timestamp }, { "da", data }};
                r.on_data = [&](nlohmann::json &resp) {
                    run_msg_websocket_response m;
                    m.response = resp;
                    cmd_run_queue.push_move(m);
                };

                ws_worker.push_move_new_request(r);
            }

            sent_end_message = true;
        }
    }
}

}}
