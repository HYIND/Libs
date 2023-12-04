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
#include <atomic>
#elif _WIN32
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <mswsock.h>
#pragma comment(lib, "Ws2_32.lib")
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
	bool enable = true;
	int read = 0;

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
	bool isWait = false;
	bool time_out = false;
};

class Net
{
public:
	Net(SocketType type = SocketType::TCP, bool isclient = false);

#ifdef __linux__
	EXPORT_FUNC int GetFd();
#elif _WIN32
	EXPORT_FUNC SOCKET GetSocket();
#endif
	EXPORT_FUNC SocketType GetType();
	EXPORT_FUNC sockaddr_in GetAddr();
	EXPORT_FUNC char *GetIPAddr();
	EXPORT_FUNC uint16_t GetPort();
	EXPORT_FUNC NetType GetNetType();
	EXPORT_FUNC bool ValidSocket();

public:
#ifdef __linux__
	EXPORT_FUNC virtual void OnRDHUP() = 0;			// 对端关闭事件，即断开连接
	EXPORT_FUNC virtual void OnEPOLLIN(int fd) = 0; // 可读事件
#elif _WIN32
	EXPORT_FUNC virtual void OnRDHUP(){};								   // 对端关闭事件，即断开连接
	EXPORT_FUNC virtual void OnREAD(SOCKET socket, Buffer &buffer){};	   // 可读事件
	EXPORT_FUNC virtual void OnACCEPT(SOCKET socket, sockaddr_in *addr){}; // 可读事件
#endif

protected:
	sockaddr_in _addr;
#ifdef __linux__
	int _fd = -1;
#elif _WIN32
	SOCKET _socket = INVALID_SOCKET;
#endif
	SocketType _type = SocketType::TCP;
	bool _isclient;
};

// 客户端(连接对象)
class NetClient : public Net
{

public:
	EXPORT_FUNC NetClient(SocketType type = SocketType::TCP);
	EXPORT_FUNC ~NetClient();
	EXPORT_FUNC bool Connet(const std::string &IP, uint16_t Port);
#ifdef __linux__
	EXPORT_FUNC void Apply(const int fd, const sockaddr_in &sockaddr, const SocketType type);
#elif _WIN32
	EXPORT_FUNC void Apply(const SOCKET socket, const sockaddr_in &sockaddr, const SocketType type);
#endif
	EXPORT_FUNC bool Release();
	EXPORT_FUNC bool AsyncSend(const Buffer &buffer, int ack = -1);		// 异步发送，不关心返回结果
	EXPORT_FUNC bool AwaitSend(const Buffer &buffer, Buffer &response); // 等待返回结果的发送，关心返回的结果
	EXPORT_FUNC int Recv(Buffer &buffer, int length);
	EXPORT_FUNC void BindMessageCallBack(std::function<void(NetClient *, Package *, Buffer *AckResponse)> callback);
	EXPORT_FUNC void BindRDHUPCallBack(std::function<void(NetClient *)> callback);

	EXPORT_FUNC SafeQueue<Package *> &GetRecvData();
	EXPORT_FUNC SafeQueue<Package *> &GetSendData();
	EXPORT_FUNC std::mutex &GetSendMtx();

public:
#ifdef __linux__
	EXPORT_FUNC virtual void OnRDHUP();
	EXPORT_FUNC virtual void OnEPOLLIN(int fd);
#elif _WIN32
	EXPORT_FUNC virtual void OnRDHUP();
	EXPORT_FUNC virtual void OnREAD(SOCKET socket, Buffer &buffer);
#endif

private:
	std::atomic<int> seq;
	SafeQueue<Package *> _RecvDatas;
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
	EXPORT_FUNC NetListener(SocketType type = SocketType::TCP);
	EXPORT_FUNC ~NetListener();
	EXPORT_FUNC bool Listen(const std::string &IP, int Port);
	EXPORT_FUNC bool ReleaseListener();
	EXPORT_FUNC bool ReleaseClients();
	EXPORT_FUNC void BindAcceptCallBack(std::function<void(NetClient *)> callback);

public:
#ifdef __linux__
	virtual void OnRDHUP();
	virtual void OnEPOLLIN(int fd);
#elif _WIN32
	virtual void OnRDHUP();
	virtual void OnACCEPT(SOCKET socket, sockaddr_in *addr);
#endif

private:
	std::map<int, NetClient *> clients;

private:
	std::function<void(NetClient *)> _callbackAccept;
};

// }