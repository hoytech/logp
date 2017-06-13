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

        conf = readMsg();
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

    void sendMsg(nlohmann::json &j, bool sync) {
        std::string output = j.dump();
        output += "\n";

        auto ret = orig_write(fd, output.data(), output.size());
        if (ret < 0 || (size_t)ret != output.size()) {
            std::cerr << "didn't write properly" << std::endl; // FIXME: use orig_write(2, ...) ?
            return;
        }

        if (sync) {
            readMsg();
        }
    }

    nlohmann::json conf;

  private:
    int fd = -1;
    std::string buffer;
};

Preloader p;


static std::function<int(int fd, const struct sockaddr *addr, socklen_t addrlen)> _build_func_connect_bind(const char *func_name) {
    auto orig = reinterpret_cast<int(*)(int fd, const struct sockaddr *addr, socklen_t addrlen)>(dlsym(RTLD_NEXT, func_name));

    if (!p.conf["funcs"].count(func_name)) return orig;
    auto &spec = p.conf["funcs"][func_name];

    auto sync = (spec["action"] == "sync");

    if (spec["when"] == "before") {
        return [=](int fd, const struct sockaddr *addr, socklen_t addrlen){
            {
                nlohmann::json details = { { "func", func_name } };
                p.sendMsg(details, sync);
            }
            return orig(fd, addr, addrlen);
        };
    } else {
        return [=](int fd, const struct sockaddr *addr, socklen_t addrlen){
            auto ret = orig(fd, addr, addrlen);
            {
                nlohmann::json details = { { "func", func_name } };
                details["ret"] = ret;
                p.sendMsg(details, sync);
            }
            return ret;
        };
    }
}

__attribute__ ((visibility ("default")))
int connect(int fd, const struct sockaddr *addr, socklen_t addrlen) {
    static std::function<int(int fd, const struct sockaddr *addr, socklen_t addrlen)> f = _build_func_connect_bind("connect");
    return f(fd, addr, addrlen);
}

__attribute__ ((visibility ("default")))
int bind(int fd, const struct sockaddr *addr, socklen_t addrlen) {
    static std::function<int(int fd, const struct sockaddr *addr, socklen_t addrlen)> f = _build_func_connect_bind("bind");
    return f(fd, addr, addrlen);
}
