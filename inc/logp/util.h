#pragma once

#include <string>
#include <sstream>

#include "logp/config.h"


namespace logp { namespace util {

void make_fd_nonblocking(int fd);

std::string get_home_dir();

uint64_t curr_time();

extern bool use_ansi_colours;

std::string colour_bold(std::string s);
std::string colour_red(std::string s);
std::string colour_green(std::string s);

void sleep_seconds(int seconds);
void sleep_forever();

class printer {
  public:
    printer(int colour_=0) : colour(colour_) {}
    ~printer() {
        std::string s(stringstream.str());
        if (colour == 1) s = logp::util::colour_red(s);
        fprintf(stderr, "logp: %s\n", s.c_str());
    }
    std::ostringstream &get() {
        return stringstream;
    }
  private:
    std::ostringstream stringstream;
    int colour = 0;
};

#define PRINT_ERROR if (::conf.verbosity >= -1) logp::util::printer(1).get() << "error: "
#define PRINT_WARNING if (::conf.verbosity >= 0) logp::util::printer().get() << "warning: "
#define PRINT_INFO if (::conf.verbosity >= 1) logp::util::printer().get() << "info: "
#define PRINT_DEBUG if (::conf.verbosity >= 2) logp::util::printer().get() << "debug: "

}}
