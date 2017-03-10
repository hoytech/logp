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
#include <thread>

#include "nlohmann/json.hpp"
#include "hoytech/timer.h"

#include "logp/cmd/run.h"
#include "logp/websocket.h"
#include "logp/signalwatcher.h"
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



enum class run_msg_type { PROCESS_EXITED, WEBSOCKET_RESPONSE };

class run_msg {
  public:
    run_msg(run_msg_type type_) : type(type_) {}

    run_msg_type type;

    // PROCESS_EXITED
    int pid = 0;
    int wait_status = 0;
    uint64_t timestamp = 0;
    struct rusage resource_usage;


    // WEBSOCKET_RESPONSE
    nlohmann::json response;
};


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
            run_msg m(run_msg_type::PROCESS_EXITED);
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

    pid_t fork_ret = fork();

    if (fork_ret == -1) {
        PRINT_ERROR << "unable to fork: " << strerror(errno);
        _exit(1);
    } else if (fork_ret == 0) {
        sigwatcher.unblock();
        execvp(my_argv[optind], my_argv+optind);
        PRINT_ERROR << "Couldn't exec " << my_argv[optind] << " : " << strerror(errno);
        _exit(1);
    }


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
            r.body = {{ "st", start_timestamp }, { "da", data }, { "hb", ::conf.heartbeat_interval }};
            r.on_data = [&](nlohmann::json &resp) {
                run_msg m(run_msg_type::WEBSOCKET_RESPONSE);
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
        run_msg m = cmd_run_queue.pop();

        if (m.type == run_msg_type::PROCESS_EXITED) {
            if (m.pid == fork_ret) {
                end_timestamp = m.timestamp;
                wait_status = m.wait_status;
                pid_exited = true;

                PRINT_INFO << "Process exited (" <<
                    (WIFEXITED(wait_status) ? (std::string("status ") + std::to_string(WEXITSTATUS(wait_status)))
                     : WIFSIGNALED(wait_status) ? (std::string("signal ") + std::to_string(WTERMSIG(wait_status)))
                     : "other")
                    << ")"
                ;

                PRINT_DEBUG << "Minor page faults: " << m.resource_usage.ru_minflt;

                kill_timeout_normal_shutdown = true;
                kill_signal_handler();
            }
        } else if (m.type == run_msg_type::WEBSOCKET_RESPONSE) {
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
        }

        if (pid_exited && have_event_id && !sent_end_message) {
            {
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

                {
                    logp::websocket::request r;

                    r.op = "add";
                    r.body = {{ "ev", event_id }, { "en", end_timestamp }, { "da", data }};
                    r.on_data = [&](nlohmann::json &resp) {
                        run_msg m(run_msg_type::WEBSOCKET_RESPONSE);
                        m.response = resp;
                        cmd_run_queue.push_move(m);
                    };

                    ws_worker.push_move_new_request(r);
                }
            }

            sent_end_message = true;
        }
    }
}

}}
