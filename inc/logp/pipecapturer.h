#pragma once

#include <unistd.h>

#include <string>
#include <functional>
#include <thread>
#include <mutex>

#include "hoytech/timer.h"

#include "logp/util.h"


namespace logp {

class pipe_capturer {
  public:
    pipe_capturer(int fd_, hoytech::timer &timer_, std::function<void(std::string &, uint64_t)> data_cb_, std::function<void()> end_cb_)
                : fd(fd_), timer(timer_), data_cb(data_cb_), end_cb(end_cb_) {
        if (pipe(pipe_descs) != 0) {
            PRINT_ERROR << "unable to create descriptor capture pipe: " << strerror(errno);
            exit(1);
        }
    }

    void child() {
        dup2(pipe_descs[1], fd);
        close(pipe_descs[0]);
        close(pipe_descs[1]);
        pipe_descs[0] = pipe_descs[1] = -1;
    }

    void parent() {
        close(pipe_descs[1]);
        pipe_descs[1] = -1;

        t = std::thread([this]() {
            while(1) {
                std::string buf;
                buf.resize(4096);

                read_again:
                ssize_t ret = ::read(pipe_descs[0], &buf[0], 4096);
                if (ret <= 0) {
                    if (ret == -1 && errno == EINTR) goto read_again;
                    pipe_closed();
                    return;
                }

                uint64_t timestamp = logp::util::curr_time();

                buf.resize(ret);

                write_again:
                ssize_t writeret = ::write(fd, buf.data(), buf.size());
                if (writeret <= 0) {
                    if (ret == -1 && errno == EINTR) goto write_again;
                    pipe_closed();
                    return;
                }

                new_data(buf, timestamp);
            }
        });
    }

  private:
    void new_data(std::string &buf, uint64_t timestamp) {
        std::unique_lock<std::mutex> lock(pending_mutex);

        if (pending_buffer.size()) {
            pending_buffer.append(buf);
        } else {
            pending_buffer.swap(buf);
            pending_timestamp = timestamp;
        }

        if (!pending_timer_cancel_token) {
            pending_timer_cancel_token = timer.once(100*1000, [this](){
                pending_timer_cancel_token = 0;
                flush_pending();
            });
        }
    }

    void flush_pending() {
        std::unique_lock<std::mutex> lock(pending_mutex);

        if (pending_buffer.size()) {
            data_cb(pending_buffer, pending_timestamp);
            pending_buffer.clear();
        }

        if (pending_timer_cancel_token) {
            timer.cancel(pending_timer_cancel_token);
            pending_timer_cancel_token = 0;
        }
    }

    void pipe_closed() {
        close(pipe_descs[0]);
        pipe_descs[0] = -1;
        flush_pending();
        end_cb();
    }

    const int fd;
    hoytech::timer &timer;
    int pipe_descs[2];
    std::function<void(std::string &, uint64_t timestamp)> data_cb;
    std::function<void()> end_cb;
    std::thread t;

    std::mutex pending_mutex;
    std::string pending_buffer;
    uint64_t pending_timestamp;
    hoytech::timer::cancel_token pending_timer_cancel_token = 0;
};

}
