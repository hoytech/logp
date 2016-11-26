#pragma once

#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <iostream>
#include <stdexcept>

#include "nlohmann/json.hpp"

#include "logp/signalwatcher.h"
#include "logp/messages.h"
#include "logp/websocket.h"


namespace logp { namespace cmd {


class run {
  public:
    void usage() {
        std::cerr << "Usage: logp run ..." << std::endl;
        exit(1);
    }

    run(int argc, char **argv) {
        int arg, option_index;

        bool capture_stderr = false;
        bool capture_stdout = false;

        struct option long_options[] = {
            {"help", no_argument, 0, 'h'},
            {"stderr", no_argument, 0, 0},
            {"stdout", no_argument, 0, 0},
            {0, 0, 0, 0}
        };

        optind = 1;
        while ((arg = getopt_long_only(argc, argv, "+c:", long_options, &option_index)) != -1) {
            switch (arg) {
              case '?':
              case 'h':
                usage();

              case 0:
                if (strcmp(long_options[option_index].name, "stderr") == 0) {
                    capture_stderr = true;
                } else if (strcmp(long_options[option_index].name, "stdout") == 0) {
                    capture_stdout = true;
                }
                break;
            };
        }



        hoytech::protected_queue<logp::msg::cmd_run> cmd_run_queue;




        logp::signal_watcher sigwatcher;

        sigwatcher.subscribe(SIGCHLD, [&]() {
            struct timeval tv;
            gettimeofday(&tv, nullptr);

            int status;
            pid_t pid = waitpid(-1, &status, WNOHANG);
            if (pid > 0) {
                logp::msg::cmd_run m(logp::msg::cmd_run_msg_type::PROCESS_EXITED);
                m.pid = pid;
                m.wait_status = status;
                m.timestamp = (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
                cmd_run_queue.push_move(m);
            }
        });

        sigwatcher.run();



        logp::websocket::worker ws_worker(::conf.url);
        ws_worker.run();


        struct timeval start_tv;
        gettimeofday(&start_tv, nullptr);
        uint64_t start_timestamp = (uint64_t)start_tv.tv_sec * 1000000 + start_tv.tv_usec;

        pid_t fork_ret = fork();

        if (fork_ret == -1) {
            throw std::runtime_error(std::string("unable to fork: ") + strerror(errno));
        } else if (fork_ret == 0) {
            execvp(argv[0], argv);
            std::cerr << "Couldn't exec " << argv[0] << " : " << strerror(errno) << std::endl;
            _exit(1);
        }


        {
            nlohmann::json j = {{ "start", start_timestamp }, { "data", {{ "stuff", 1 }} }};
            std::string op("add");
            std::string msg_str = j.dump();
            ws_worker.send_message_move(op, msg_str, [&](std::string &resp) {
                logp::msg::cmd_run m(logp::msg::cmd_run_msg_type::WEBSOCKET_RESPONSE);
                m.response = resp;
                cmd_run_queue.push_move(m);
            });
        }



        bool pid_exited = false;
        uint64_t end_timestamp = 0;
        bool have_event_id = false;
        uint64_t event_id = 0;
        bool sent_end_message = false;
        int wait_status = 0;

        while (1) {
            logp::msg::cmd_run m = cmd_run_queue.pop();

            if (m.type == logp::msg::cmd_run_msg_type::PROCESS_EXITED) {
                if (m.pid == fork_ret) {
                    end_timestamp = m.timestamp;
                    wait_status = m.wait_status;
                    pid_exited = true;
                }
            } else if (m.type == logp::msg::cmd_run_msg_type::WEBSOCKET_RESPONSE) {
                if (!sent_end_message) {
                    try {
                        auto j = nlohmann::json::parse(m.response);

                        if (j["status"] == "ok") {
                            event_id = j["event_id"];
                            have_event_id = true;
                        } else {
                            std::cerr << "status was not OK on end response" << std::endl;
                        }
                    } catch (std::exception &e) {
                        std::cerr << "Unable to parse JSON body to extract event id" << std::endl;
                    }
                } else {
                    try {
                        auto j = nlohmann::json::parse(m.response);

                        if (j["status"] == "ok") {
                            exit(WEXITSTATUS(wait_status));
                        } else {
                            std::cerr << "status was not OK on end response" << std::endl;
                        }
                    } catch (std::exception &e) {
                        std::cerr << "Unable to parse JSON body to confirm end" << std::endl;
                    }
                }
            }

            if (pid_exited && have_event_id && !sent_end_message) {
                {
                    nlohmann::json j = {{ "event_id", event_id }, { "end", end_timestamp }};
                    std::string op("add");
                    std::string msg_str = j.dump();
                    ws_worker.send_message_move(op, msg_str, [&](std::string &resp) {
                        logp::msg::cmd_run m(logp::msg::cmd_run_msg_type::WEBSOCKET_RESPONSE);
                        m.response = resp;
                        cmd_run_queue.push_move(m);
                    });
                }

                sent_end_message = true;
            }
        }
    }
};


}}
