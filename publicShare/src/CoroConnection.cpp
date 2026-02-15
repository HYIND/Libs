#include "Coroutine.h"

#ifdef __linux__
#include "CoroutineScheduler_Linux.h"
#else 
#include "CoroutineScheduler_Win.h"
#endif

#ifdef __linux__
#include <arpa/inet.h>
#include <cstring>
#endif

#ifdef __linux__
BaseSocket NewClientSocket(const std::string& IP, uint16_t port, __socket_type protocol, sockaddr_in& sock_addr)
{
	memset(&sock_addr, 0, sizeof(sock_addr));
	sock_addr.sin_family = AF_INET;
	sock_addr.sin_port = htons(port);

	sock_addr.sin_addr.s_addr = inet_addr(IP.c_str());

	return ::socket(PF_INET, protocol, 0);
}

#elif _WIN32
BaseSocket NewClientSocket(const std::string& IP, uint16_t port, int protocol, sockaddr_in& sock_addr)
{
	ZeroMemory(&sock_addr, sizeof(sock_addr));
	sock_addr.sin_family = AF_INET;
	sock_addr.sin_port = htons(port);

	inet_pton(AF_INET, IP.c_str(), &(sock_addr.sin_addr.s_addr));

	// 根据协议确定套接字类型
	int type;
	switch (protocol) {
	case IPPROTO_TCP:
		type = SOCK_STREAM;
		break;
	case IPPROTO_UDP:
		type = SOCK_DGRAM;
		break;
	default:
		type = SOCK_RAW;
	}

	return WSASocket(sock_addr.sin_family, type, protocol, NULL, 0, WSA_FLAG_OVERLAPPED);
}
#endif

CoConnection::Handle::Handle()
	: socket(0), active(true), corodone{ false }
{
}

CoConnection::Handle::~Handle()
{
}

CoConnection::Awaiter::Awaiter(std::shared_ptr<Handle> handle)
	: handle(handle)
{
}

bool CoConnection::Awaiter::await_ready()
{
	return !handle || !handle->active;
}

void CoConnection::Awaiter::await_suspend(std::coroutine_handle<> coro)
{
	auto trytoresume = [&]() -> void
	{
		bool expected = false;
		if (handle->corodone.compare_exchange_strong(expected, true))
			coro.resume();
	};

	if (!handle || !handle->active)
	{
		trytoresume();
		return;
	}

	if (!handle->active || coro.done())
	{
		trytoresume();
		return;
	}

	{
		LockGuard lock(handle->corolock);
		if (!handle->active || coro.done())
		{
			trytoresume();
			return;
		}
		handle->coroutine = coro;
	}
}

BaseSocket CoConnection::Awaiter::await_resume()
{
	if (handle)
		return handle->socket;
	return 0;
}

CoConnection::CoConnection(const std::string& ip, const int port)
{
#ifdef _WIN32

	static std::atomic<bool> initwin{ false };
	bool execpted = false;
	if (initwin.compare_exchange_strong(execpted, true))
	{
		WSADATA wsa;
		WSAStartup(MAKEWORD(2, 2), &wsa);
	}
#endif

	sockaddr_in localaddr, remoteaddr;
	memset(&localaddr, 0, sizeof(localaddr));
	localaddr.sin_family = AF_INET;
	localaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	localaddr.sin_port = htons(0);

	BaseSocket socket = NewClientSocket(ip, port, IPPROTO_TCP, remoteaddr);

	if (socket <= 0)
	{
		CoCloseSocket(socket);
		return;
	}

	if (::bind(socket, (sockaddr*)&localaddr, sizeof(struct sockaddr)) == SOCKET_ERROR)
	{
		CoCloseSocket(socket);
		return;
	}

#ifdef _WIN32
	int result = connect(socket, (struct sockaddr*)&remoteaddr, sizeof(struct sockaddr));
	if (result < 0)
	{
		perror("connect socket error");
		return;
	}

	handle = std::make_shared<Handle>();
	handle->active = false;
	handle->socket = socket;

#elif __linux__
	handle = CoroutineScheduler::Instance()->create_connection(socket, localaddr, remoteaddr);
#endif
}

CoConnection::~CoConnection() {}

CoConnection::Awaiter CoConnection::operator co_await()
{
	return CoConnection::Awaiter(handle);
}