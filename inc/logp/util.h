#pragma once

#include <string>

namespace logp { namespace util {

void make_fd_nonblocking(int fd);

std::string get_home_dir();

uint64_t curr_time();

std::string colour_bold(std::string s);
std::string colour_red(std::string s);
std::string colour_green(std::string s);

extern bool use_ansi_colours;

void sleep_seconds(int seconds);
void sleep_forever();

}}
