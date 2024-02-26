
/*
	该文件是对TCP传输层协议下的基础连接对象的封装
	其中包含一个TCP连接监听器，TCP连接客户端
 */

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

#include "Helper/Buffer.h"
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

class BaseTransportConnection
{
public:
	BaseTransportConnection(SocketType type = SocketType::TCP, bool isclient = false);

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

// TCP传输层客户端(连接对象)
class TCPTransportConnection : public BaseTransportConnection
{

public:
	EXPORT_FUNC TCPTransportConnection();
	EXPORT_FUNC ~TCPTransportConnection();
	EXPORT_FUNC bool Connect(const std::string &IP, uint16_t Port);
#ifdef __linux__
	EXPORT_FUNC void Apply(const int fd, const sockaddr_in &sockaddr, const SocketType type);
#elif _WIN32
	EXPORT_FUNC void Apply(const SOCKET socket, const sockaddr_in &sockaddr, const SocketType type);
#endif
	EXPORT_FUNC bool Release();
	EXPORT_FUNC bool Send(const Buffer &buffer);
	EXPORT_FUNC int Read(Buffer &buffer, int length);

	EXPORT_FUNC void BindBufferCallBack(std::function<void(TCPTransportConnection *, Buffer *)> callback);
	EXPORT_FUNC void BindRDHUPCallBack(std::function<void(TCPTransportConnection *)> callback);

	EXPORT_FUNC SafeQueue<Buffer *> &GetRecvData();
	EXPORT_FUNC SafeQueue<Buffer *> &GetSendData();
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
	SafeQueue<Buffer *> _RecvDatas;
	SafeQueue<Buffer *> _SendDatas;

private:
	std::function<void(TCPTransportConnection *, Buffer *)> _callbackBuffer;
	std::function<void(TCPTransportConnection *)> _callbackRDHUP;
	std::mutex _SendResMtx;
};

// TCP传输层监听器
class TCPTransportListener : public BaseTransportConnection
{

public:
	EXPORT_FUNC TCPTransportListener();
	EXPORT_FUNC ~TCPTransportListener();
	EXPORT_FUNC bool Listen(const std::string &IP, int Port);
	EXPORT_FUNC bool ReleaseListener();
	EXPORT_FUNC bool ReleaseClients();
	EXPORT_FUNC void BindAcceptCallBack(std::function<void(TCPTransportConnection *)> callback);

public:
#ifdef __linux__
	virtual void OnRDHUP();
	virtual void OnEPOLLIN(int fd);
#elif _WIN32
	virtual void OnRDHUP();
	virtual void OnACCEPT(SOCKET socket, sockaddr_in *addr);
#endif

private:
	std::map<int, TCPTransportConnection *> clients;

private:
	std::function<void(TCPTransportConnection *)> _callbackAccept;
};

// }