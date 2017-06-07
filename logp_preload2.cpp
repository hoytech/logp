#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <dlfcn.h>

#include <iostream>
#include <vector>
#include <string>
#include <fstream>

#include <nlohmann/json.hpp>



static std::vector<std::string> get_argv() {
    std::vector<std::string> output;

#ifdef __linux__
    std::ifstream infile("/proc/self/cmdline");

    std::string arg;
    while (std::getline(infile, arg, '\0')) {
        output.push_back(arg);
    }
#endif

    return output;
}


static ssize_t(*orig_write)(int fd, const void *buf, size_t count);
static ssize_t(*orig_read)(int fd, const void *buf, size_t count);
static int (*orig_connect)(int fd, const struct sockaddr *addr, socklen_t addrlen);


class Preloader {
  public:
    Preloader() {
        orig_write = reinterpret_cast<ssize_t(*)(int fd, const void *buf, size_t count)>(dlsym(RTLD_NEXT, "write"));
        orig_read = reinterpret_cast<ssize_t(*)(int fd, const void *buf, size_t count)>(dlsym(RTLD_NEXT, "read"));
        orig_connect = reinterpret_cast<int(*)(int fd, const struct sockaddr *addr, socklen_t addrlen)>(dlsym(RTLD_NEXT, "connect"));

        char *logp_socket_path = getenv("LOGP_SOCKET_PATH");
        if (!logp_socket_path) return;

        int sock_type = SOCK_STREAM;
#ifdef SOCK_CLOEXEC
        sock_type |= SOCK_CLOEXEC;
#endif

        fd = socket(AF_UNIX, sock_type, 0);
        if (fd == -1) return;

#ifndef SOCK_CLOEXEC
        fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC);
#endif

        struct sockaddr_un sa;

        sa.sun_family = AF_UNIX;
        size_t path_len = strlen(logp_socket_path);
        if (path_len >= sizeof(sa.sun_path)) return;
        strcpy(sa.sun_path, logp_socket_path);

        if (orig_connect(fd, reinterpret_cast<const sockaddr*>(&sa), sizeof(sa)) == -1) {
            close(fd);
            fd = -1;
            return;
        }

        nlohmann::json output = {
            { "type", "proc_start" },
            { "pid", getpid() },
            { "ppid", getppid() }
        };

        auto argv = get_argv();
        if (argv.size()) output["argv"] = argv;

        std::string output_str = output.dump();
        output_str += "\n";

        auto ret = orig_write(fd, output_str.c_str(), output_str.size());
        if (ret <= 0) {
            close(fd);
            fd = -1;
            return;
        }

        auto j = readMsg();
        std::cerr << "IN PRELOAD GOT: " << j.dump() << std::endl;
    }

    nlohmann::json readMsg() {
        while(1) {
            auto len = buffer.find('\n');
            if (len != std::string::npos) {
                auto j = nlohmann::json::parse(buffer.substr(0, len));
                buffer.erase(0, len+1);
                return j;
            }

            char tmpbuf[4096];
            auto ret = orig_read(fd, tmpbuf, sizeof(tmpbuf));

            if (ret <= 0) {
                close(fd);
                fd = -1;
                return nlohmann::json({}); 
            }

            buffer += std::string(tmpbuf, (size_t)ret);
        }
    }

  private:
    int fd = -1;
    std::string buffer;
};

Preloader p;


__attribute__ ((visibility ("default")))
ssize_t write(int fd, const void *buf, size_t count) {
    static auto orig = reinterpret_cast<ssize_t(*)(int, const void *, size_t)>(dlsym(RTLD_NEXT, __FUNCTION__));
    return orig(fd, buf, count);
}

__attribute__ ((visibility ("default")))
ssize_t read(int fd, void *buf, size_t count) {
    static auto orig = reinterpret_cast<ssize_t(*)(int, const void *, size_t)>(dlsym(RTLD_NEXT, __FUNCTION__));
    return orig(fd, buf, count);
}

__attribute__ ((visibility ("default")))
int connect(int fd, const struct sockaddr *addr, socklen_t addrlen) {
    static auto orig = reinterpret_cast<int(*)(int fd, const struct sockaddr *addr, socklen_t addrlen)>(dlsym(RTLD_NEXT, __FUNCTION__));
    std::cerr << "BINGBING " << __FUNCTION__ << std::endl;
    return orig(fd, addr, addrlen);
}

__attribute__ ((visibility ("default")))
int bind(int fd, const struct sockaddr *addr, socklen_t addrlen) {
    static auto orig = reinterpret_cast<int(*)(int fd, const struct sockaddr *addr, socklen_t addrlen)>(dlsym(RTLD_NEXT, __FUNCTION__));
    std::cerr << "BINGBING " << __FUNCTION__ << std::endl;
    return orig(fd, addr, addrlen);
}
