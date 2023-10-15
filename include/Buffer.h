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

class Buffer
{

public:
    Buffer();
    Buffer(const int length);
    Buffer(const char *source, int length);
    Buffer(const std::string &source);
    ~Buffer();

    void *Data() const;
    int Length() const;
    int Postion() const;

    void CopyFromBuf(const char *buf, int length); // 拷贝
    void CopyFromBuf(const Buffer &other);
    void QuoteFromBuf(char *buf, int length); // 以引用的形式占有一段内存
    void QuoteFromBuf(Buffer &other);

    int Write(const Buffer &other);               // 从pos开始，向当前流写入数据，数据来源为其他流
    int Write(const std::string &str);            // 从pos开始，向当前流写入数据
    int Write(const void *buf, const int length); // 从pos开始，向当前流写入数据
    int Read(void **buf, const int length);       // 从pos开始，读出当前流内的数据
    int Seek(const int index);

    void Release();

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
