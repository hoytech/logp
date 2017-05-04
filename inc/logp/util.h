#pragma once

#include <sys/time.h>

#include <string>
#include <sstream>
#include <stdexcept>

#include "logp/config.h"




namespace logp {

// Based on https://www.agwa.name/projects/templates/

inline void build_string(std::ostream&) { }

template<class First, class... Rest>
inline void build_string(std::ostream& o, const First& value, const Rest&... rest) {
    o << value;
    build_string(o, rest...);
}

template<class... T>
std::string concat_string(const T&... value) {
    std::ostringstream o;
    build_string(o, value...);
    return o.str();
}

template<class... T>
std::runtime_error error(const T&... value) {
    std::ostringstream o;
    build_string(o, value...);
    return std::runtime_error(o.str());
}

}





namespace logp { namespace util {

void make_fd_nonblocking(int fd);

std::string get_home_dir();

uint64_t curr_time();

uint64_t timeval_to_usecs(struct timeval &);




std::string utf8_encode_binary(std::string &input);
std::string utf8_decode_binary(std::string &input);


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
