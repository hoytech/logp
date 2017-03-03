#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <pwd.h>

#include <cstdlib>
#include <thread>
#include <chrono>
#include <string>

#include "logp/util.h"


namespace logp { namespace util {

void make_fd_nonblocking(int fd) {
    int flags;

    if ((flags = fcntl(fd, F_GETFL, 0)) == -1) {
        throw logp::error("unable to fcntl(F_GETFL): ", strerror(errno));
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        throw logp::error("unable to fcntl(F_GETFL): ", strerror(errno));
    }
}

std::string get_home_dir() {
    char *home_dir = std::getenv("HOME");

    if (home_dir) return std::string(home_dir);

    home_dir = getpwuid(getuid())->pw_dir;

    if (home_dir) return std::string(home_dir);

    return std::string("");
}

uint64_t curr_time() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}




bool use_ansi_colours = false;

std::string colour_bold(std::string s) {
    if (use_ansi_colours) return std::string("\033[1m") + s + std::string("\033[0m");
    return s;
}

std::string colour_red(std::string s) {
    if (use_ansi_colours) return std::string("\033[0;31;1m") + s + std::string("\033[0m");
    return s;
}

std::string colour_green(std::string s) {
    if (use_ansi_colours) return std::string("\033[0;32;1m") + s + std::string("\033[0m");
    return s;
}


void sleep_seconds(int seconds) {
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
}

void sleep_forever() {
    std::this_thread::sleep_until(std::chrono::time_point<std::chrono::system_clock>::max());
}


}}
