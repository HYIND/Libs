#pragma once

#ifdef __linux__
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/time.h>
#include <sys/timerfd.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include "fmt/core.h"
#endif

#include <string.h>
#include <signal.h>
#include <thread>
#include <vector>
#include <algorithm>
#include <fcntl.h>
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
#include <algorithm>
#include <iomanip>
#include <chrono>
#include <functional>
#include <random>
#include <shared_mutex>

#ifdef _WIN32
#define EXPORT_FUNC __declspec(dllexport)
#elif __linux__
#define EXPORT_FUNC
#endif

class Buffer
{

public:
    EXPORT_FUNC Buffer();
    EXPORT_FUNC Buffer(const int length);
    EXPORT_FUNC Buffer(const char *source, int length);
    EXPORT_FUNC Buffer(const std::string &source);
    EXPORT_FUNC ~Buffer();

    EXPORT_FUNC void *Data() const;
    EXPORT_FUNC int Length() const;
    EXPORT_FUNC int Postion() const;

    EXPORT_FUNC void CopyFromBuf(const char *buf, int length); // 拷贝
    EXPORT_FUNC void CopyFromBuf(const Buffer &other);
    EXPORT_FUNC void QuoteFromBuf(char *buf, int length); // 以引用的形式占有一段内存
    EXPORT_FUNC void QuoteFromBuf(Buffer &other);

    EXPORT_FUNC int Write(const Buffer &other);               // 从pos开始，向当前流写入数据，数据来源为其他流
    EXPORT_FUNC int Write(const std::string &str);            // 从pos开始，向当前流写入数据
    EXPORT_FUNC int Write(const void *buf, const int length); // 从pos开始，向当前流写入数据
    EXPORT_FUNC int Read(void **buf, const int length);       // 从pos开始，读出当前流内的数据
    EXPORT_FUNC int Seek(const int index);

    EXPORT_FUNC void Release();
    EXPORT_FUNC void ReSize(const int length);

private:
    char *_buf = nullptr;
    int _length = 0;
    int _pos = 0;
};

#define SAFE_DELETE(x) \
    if (x)             \
    {                  \
        delete x;      \
        x = nullptr;   \
    }

#define SAFE_DELETE_ARRAY(x) \
    if (x)                   \
    {                        \
        delete[] x;          \
        x = nullptr;         \
    }
