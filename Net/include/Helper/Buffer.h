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

// #define min(a, b) \
//     if (a <= b)   \
//         return a; \
//     else          \
//         return b

class Buffer
{

public:
    EXPORT_FUNC Buffer();
    EXPORT_FUNC Buffer(const Buffer &other);
    EXPORT_FUNC Buffer(const uint64_t length);
    EXPORT_FUNC Buffer(const char *source, uint64_t length);
    EXPORT_FUNC Buffer(const std::string &source);
    EXPORT_FUNC ~Buffer();

    EXPORT_FUNC void *Data() const;
    EXPORT_FUNC char *Byte() const;
    EXPORT_FUNC uint64_t Length() const;
    EXPORT_FUNC uint64_t Postion() const;
    EXPORT_FUNC uint64_t Remaind() const;

    EXPORT_FUNC void CopyFromBuf(const char *buf, uint64_t length); // 拷贝
    EXPORT_FUNC void CopyFromBuf(const Buffer &other);
    EXPORT_FUNC void QuoteFromBuf(char *buf, uint64_t length); // 以引用的形式占有一段内存
    EXPORT_FUNC void QuoteFromBuf(Buffer &other);

    /**
     * 以下读写操作均与pos相关，并引起相关流pos变化
     */

    /** 以下操作引起当前流pos变化*/
    EXPORT_FUNC uint64_t Write(const Buffer &other);                    // 从pos开始，向当前流写入数据，数据来源为其他流
    EXPORT_FUNC uint64_t Write(const std::string &str);                 // 从pos开始，向当前流写入数据
    EXPORT_FUNC uint64_t Write(const void *buf, const uint64_t length); // 从pos开始，向当前流写入数据
    EXPORT_FUNC uint64_t Read(void *buf, const uint64_t length);        // 从pos开始，读出当前流内的数据,返回实际读取字节数
    EXPORT_FUNC uint64_t Seek(const uint64_t index);                    // 设置pos

    /** 以下操作引起其他流pos变化*/
    EXPORT_FUNC uint64_t Append(Buffer &other);                  // 从其他流的pos开始读取剩余内容，向当前流末尾追加数据，返回实际追加的字节数，该操作会引起其他流pos变化
    EXPORT_FUNC uint64_t Append(Buffer &other, uint64_t length); // 从其他流的pos开始读取length字节，向当前流末尾追加数据，返回实际追加的字节数，该操作会引起其他流pos变化

    /** 以下操作引起当前流与其他流pos变化*/
    EXPORT_FUNC uint64_t WriteFromOtherBufferPos(Buffer &other);                  // 从当前流pos开始，从其他流的pos开始读取剩余内容，向当前流写入，返回实际读取的字节数，该操作会引起其他流的pos变化
    EXPORT_FUNC uint64_t WriteFromOtherBufferPos(Buffer &other, uint64_t length); // 从当前流pos开始，从其他流的pos开始读取length字节，向当前流写入，返回实际读取的字节数，该操作会引起其他流的pos变化

    EXPORT_FUNC void Release();                                       // 释放当前流，并重置pos,length
    EXPORT_FUNC void ReSize(const uint64_t length);                   // 重置流大小，可能会截断原流
    EXPORT_FUNC void Shift(const uint64_t length);                    // 从流的头部开始，移除该流的前n个字节
    EXPORT_FUNC void Unshift(const void *buf, const uint64_t length); // 在该流的头部添加n个字节

private:
    char *_buf = nullptr;
    uint64_t _length = 0;
    uint64_t _pos = 0;
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
