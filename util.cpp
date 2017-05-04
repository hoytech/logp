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

uint64_t timeval_to_usecs(struct timeval &tv) {
    return (tv.tv_sec * 1000000) + tv.tv_usec;
}




// This encoding scheme is to allow binary data to be losslessly encoded into JSON.
// * Bytes <= 0x1F are encoded as "\u0000", "\u0001", etc by the JSON encoder
// * Bytes >= 0x20 and <= 0x7F are left as is
// * Bytes >= 0x80 are UTF-8 encoded as their equivalent unicode code-points by
//   the functions below.

// Round-trip test:
// $ logp run perl -CAS -e 'print join("", map{chr} (0..255))' | md5sum
// e1cb1402564d3f0d07fc946196789c81  -
// $ logp cat -e 280 | md5sum
// e1cb1402564d3f0d07fc946196789c81  -

std::string utf8_encode_binary(std::string &input) {
    // perl -MEncode -E '@c = split(//, encode("utf-8", join("", map {chr} (128..255)))); print join("", map { sprintf("\\x%x", ord($_)) } @c)'

    static unsigned char utf8_encode_binary_table[] = "\xc2\x80\xc2\x81\xc2\x82\xc2\x83\xc2\x84\xc2\x85\xc2\x86\xc2\x87\xc2\x88\xc2\x89\xc2\x8a\xc2\x8b\xc2\x8c\xc2\x8d\xc2\x8e\xc2\x8f\xc2\x90\xc2\x91\xc2\x92\xc2\x93\xc2\x94\xc2\x95\xc2\x96\xc2\x97\xc2\x98\xc2\x99\xc2\x9a\xc2\x9b\xc2\x9c\xc2\x9d\xc2\x9e\xc2\x9f\xc2\xa0\xc2\xa1\xc2\xa2\xc2\xa3\xc2\xa4\xc2\xa5\xc2\xa6\xc2\xa7\xc2\xa8\xc2\xa9\xc2\xaa\xc2\xab\xc2\xac\xc2\xad\xc2\xae\xc2\xaf\xc2\xb0\xc2\xb1\xc2\xb2\xc2\xb3\xc2\xb4\xc2\xb5\xc2\xb6\xc2\xb7\xc2\xb8\xc2\xb9\xc2\xba\xc2\xbb\xc2\xbc\xc2\xbd\xc2\xbe\xc2\xbf\xc3\x80\xc3\x81\xc3\x82\xc3\x83\xc3\x84\xc3\x85\xc3\x86\xc3\x87\xc3\x88\xc3\x89\xc3\x8a\xc3\x8b\xc3\x8c\xc3\x8d\xc3\x8e\xc3\x8f\xc3\x90\xc3\x91\xc3\x92\xc3\x93\xc3\x94\xc3\x95\xc3\x96\xc3\x97\xc3\x98\xc3\x99\xc3\x9a\xc3\x9b\xc3\x9c\xc3\x9d\xc3\x9e\xc3\x9f\xc3\xa0\xc3\xa1\xc3\xa2\xc3\xa3\xc3\xa4\xc3\xa5\xc3\xa6\xc3\xa7\xc3\xa8\xc3\xa9\xc3\xaa\xc3\xab\xc3\xac\xc3\xad\xc3\xae\xc3\xaf\xc3\xb0\xc3\xb1\xc3\xb2\xc3\xb3\xc3\xb4\xc3\xb5\xc3\xb6\xc3\xb7\xc3\xb8\xc3\xb9\xc3\xba\xc3\xbb\xc3\xbc\xc3\xbd\xc3\xbe\xc3\xbf";

    std::string output;
    output.reserve(input.size());

    for (unsigned char c : input) {
        if (c < 0x80) {
            output += c;
        } else {
            int offset = (c - 0x80) * 2;
            output += utf8_encode_binary_table[offset];
            output += utf8_encode_binary_table[offset + 1];
        }
    }

    return output;
}

std::string utf8_decode_binary(std::string &input) {
    std::string output;
    output.reserve(input.size());

    for (size_t curr = 0; curr < input.size(); curr++) {
        unsigned char c = input.data()[curr];

        if (c < 0x80) {
            output += c;
        } else if (c == 0xc2 || c == 0xc3) {
            if (curr >= input.size() - 1) throw logp::error("premature end of string in utf8 decode input");
            unsigned char c2 = input.data()[++curr];
            if ((c2 & 0xc0) != 0x80) throw logp::error("unrecognized continuation byte in utf8 decode input: ", (int)c2);
            int code_point = ((c & 0x1f) << 6) | (c2 & 0x3f);
            if (code_point < 0x80 || code_point > 255) throw logp::error("unexpected code-point in utf8 decode input: ", code_point);
            output += static_cast<unsigned char>(code_point);
        } else {
            throw logp::error("unrecognized character in utf8 decode input: ", (int)c);
        }
    }

    return output;
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
