#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#include <stdexcept>


namespace logp { namespace util {

void make_fd_nonblocking(int fd) {
    int flags;

    if ((flags = fcntl(fd, F_GETFL, 0)) == -1) {
        throw std::runtime_error(std::string("unable to fcntl(F_GETFL): ") + strerror(errno));
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        throw std::runtime_error(std::string("unable to fcntl(F_GETFL): ") + strerror(errno));
    }
}

}}
