#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <errno.h>

#include <stdio.h>



static void append(void *data, size_t data_len, char **output, size_t *output_size, size_t *output_allocated) {
    if (*output_allocated - *output_size < data_len) {
        size_t new_size = (*output_allocated * 2) + data_len;
        output = realloc(*output, new_size);
        *output_allocated = new_size;
    }

    memcpy(*output + *output_size, data, data_len);
    *output_size += data_len;
}

static void add_field(uint16_t type, void *data, size_t data_len, char **output, size_t *output_size, size_t *output_allocated) {
    append(&type, sizeof(type), output, output_size, output_allocated);
    append(&data_len, sizeof(data_len), output, output_size, output_allocated);
    append(data, data_len, output, output_size, output_allocated);
}


static char *get_cmdline(size_t *len) {
#ifdef __linux__
    int fd = open("/proc/self/cmdline", O_RDONLY);
    if (fd == -1) return NULL;

    size_t allocated = 4096;
    char *buffer = malloc(allocated);
    if (!buffer) {
        close(fd);
        return NULL;
    }

    ssize_t ret;

    while (1) {
        ret = pread(fd, buffer, allocated, 0);

        if (ret < 0) {
            if (errno == EINTR) continue;
            close(fd);
            return NULL;
        }

        if ((size_t)ret == allocated) {
            allocated *= 2;
            buffer = realloc(buffer, allocated);
        } else {
            break;
        }
    }

    close(fd);

    *len = ret;
    return buffer;
#elif __APPLE__
    /* // FIXME:
    char ***argvp = _NSGetArgv();
    int *argcp = _NSGetArgc();
    if (argvp != nullptr && argcp != nullptr) {
        for (int i = 0; i < *argcp; i++) output.push_back((*argvp)[i]);
    }
    */
    return NULL;
#else
    return NULL;
#endif
}


static void init(void) __attribute__((constructor));
static void init() {
    char *logp_socket_path;
    int fd;
    struct sockaddr_un sa;
    char *output;
    size_t output_size;
    size_t output_allocated;

    logp_socket_path = getenv("LOGP_SOCKET_PATH");
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

    sa.sun_family = AF_UNIX;
    size_t path_len = strlen(logp_socket_path);
    if (path_len >= sizeof(sa.sun_path)) return;
    strcpy(sa.sun_path, logp_socket_path);

    if (connect(fd, (const struct sockaddr*)&sa, sizeof(sa)) == -1) {
        close(fd);
        return;
    }

    output_allocated = 4096;
    output_size = 0;
    output = malloc(output_allocated);
    if (!output) {
        close(fd);
        return;
    }

    output_size += sizeof(size_t); // leave room for total length

    {
        pid_t pid = getpid();
        add_field(1, &pid, sizeof(pid), &output, &output_size, &output_allocated);
    }

    {
        pid_t pid = getppid();
        add_field(2, &pid, sizeof(pid), &output, &output_size, &output_allocated);
    }

    {
        size_t len;
        char *cmdline = get_cmdline(&len);
        if (cmdline) {
            add_field(3, cmdline, len, &output, &output_size, &output_allocated);
            free(cmdline);
        }
    }

    memcpy(output, &output_size, sizeof(size_t));

    ssize_t write_ret = write(fd, output, output_size);
    if (write_ret <= 0) {
        close(fd);
        return;
    }

    // keep fd open so we can detect when process exits
}
