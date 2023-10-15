#pragma once

#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <thread>
#include <vector>
#include <algorithm>
#include <fcntl.h>
#include <sys/epoll.h>
#include <signal.h>
#include <iostream>
#include <assert.h>
#include <unordered_map>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <stdio.h>
#include <stdlib.h>
#include <type_traits>
#include <sys/time.h>
#include <sys/timerfd.h>
#include <algorithm>
#include <iomanip>
#include <chrono>
#include <functional>
#include <random>
#include <net/if.h>
#include <sys/ioctl.h>
#include <shared_mutex>
#include "fmt/core.h"
#include <map>

#define exit_if(r, ...)                                                                          \
    if (r)                                                                                       \
    {                                                                                            \
        printf(__VA_ARGS__);                                                                     \
        printf("%s:%d error no: %d error msg %s\n", __FILE__, __LINE__, errno, strerror(errno)); \
        exit(1);                                                                                 \
    }

void RunNetCoreLoop();