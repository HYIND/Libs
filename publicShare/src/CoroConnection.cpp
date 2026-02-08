#include "Coroutine.h"

#ifdef __linux__
#include "CoroutineScheduler_Linux.h"
#else 
#include "CoroutineScheduler_Win.h"
#endif

#ifdef __linux__
#include <arpa/inet.h>
#endif

#ifdef __linux__
BaseSocket NewClientSocket(const std::string& IP, uint16_t port, __socket_type protocol, sockaddr_in& sock_addr)
{
	memset(&sock_addr, '0', sizeof(sock_addr));
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

	return WSASocket(sock_addr.sin_family, protocol, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
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
	return handle->socket;
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

	sockaddr_in local_addr;
	local_addr.sin_family = AF_INET;
	local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	local_addr.sin_port = htons(0);

	sockaddr_in remote_addr;
	BaseSocket socket = NewClientSocket(ip, port, SOCK_STREAM, remote_addr);

	if (socket <= 0)
	{
		CoCloseSocket(socket);
		handle = std::make_shared<CoConnection::Handle>();
		handle->socket = 0;
		handle->active = false;
		return;
	}

	handle = CoroutineScheduler::Instance()->create_connection(socket, local_addr, remote_addr);
}

CoConnection::~CoConnection() {}

CoConnection::Awaiter CoConnection::operator co_await()
{
	return CoConnection::Awaiter(handle);
}