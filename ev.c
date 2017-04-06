#define EV_STANDALONE 1
#define EV_PROTOTYPES 1
#define EV_USE_NANOSLEEP EV_USE_MONOTONIC
#define EV_USE_FLOOR 1
#define EV_H <ev.h>
#define EV_CONFIG_H error

#define EV_USE_POLL 1

#ifdef __linux__
#define EV_USE_EPOLL 1
#endif

#ifdef __APPLE__
// FIXME: also use kqueue on BSD
#define EV_USE_KQUEUE 1
#endif

#include "inc/libev/ev.c"
