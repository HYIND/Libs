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

#include "Buffer.h"
#include "SafeStl.h"

// namespace Net
// {

enum NetType
{
    Listener = 1,
    Client = 2
};

enum SocketType
{
    TCP = 1,
    UDP = 2
};

struct Package
{
    int seq = 0;
    int ack = -1;
    int written = 0;
    Buffer buffer;
};

// 关注返回值的等待任务
struct AwaitTask
{
    int seq = 0;
    Buffer *respsonse;
    std::mutex _mtx;
    std::condition_variable _cv;
    bool time_out = false;
};

class Net
{
public:
    Net(SocketType type = SocketType::TCP, bool isclient = false);

    int GetFd();
    SocketType GetType();
    sockaddr_in GetAddr();
    char *GetIPAddr();
    uint16_t GetPort();
    NetType GetNetType();

public:
    virtual void OnRDHUP() = 0;         // 对端关闭事件，即断开连接
    virtual void OnEPOLLIN(int fd) = 0; // 可读事件

protected:
    sockaddr_in _addr;
    int _fd = -1;
    SocketType _type = SocketType::TCP;
    bool _isclient;
};

// 客户端(连接对象)
class NetClient : public Net
{

public:
    NetClient(SocketType type = SocketType::TCP);
    ~NetClient();
    bool Connet(const std::string &IP, uint16_t Port);
    void Apply(const int fd, const sockaddr_in &sockaddr, const SocketType type);
    bool Release();
    bool AsyncSend(const Buffer &buffer, int ack = -1);     // 异步发送，不关心返回结果
    bool AwaitSend(const Buffer &buffer, Buffer &response); // 等待返回结果的发送，关心返回的结果
    int Recv(Buffer &buffer, int length);
    void BindMessageCallBack(std::function<void(NetClient *, Package *, Buffer *AckResponse)> callback);
    void BindRDHUPCallBack(std::function<void(NetClient *)> callback);

    std::map<int, Package *> &GetRecvData();
    SafeQueue<Package *> &GetSendData();
    std::mutex &GetSendMtx();

public:
    virtual void OnRDHUP();
    virtual void OnEPOLLIN(int fd);

private:
    int seq = 0;
    std::map<int, Package *> _RecvDatas;
    SafeQueue<Package *> _SendDatas;
    std::map<int, AwaitTask *> _AwaitMap; // seq->AwaitTask

private:
    std::function<void(NetClient *, Package *, Buffer *response)> _callbackMessage;
    std::function<void(NetClient *)> _callbackRDHUP;
    std::mutex _SendResMtx;
};

// 监听器
class NetListener : public Net
{

public:
    NetListener(SocketType type = SocketType::TCP);
    ~NetListener();
    bool Listen(const std::string &IP, int Port);
    bool ReleaseListener();
    bool ReleaseClients();
    void BindAcceptCallBack(std::function<void(NetClient *)> callback);

public:
    virtual void OnRDHUP();
    virtual void OnEPOLLIN(int fd);

private:
    std::map<int, NetClient *> clients;

private:
    std::function<void(NetClient *)> _callbackAccept;
};

// }