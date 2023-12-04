#include "NetCoredef.h"
#include "NetCore.h"

using namespace std;

Buffer HeartBuffer("23388990");

#ifdef __linux__
void setnonblocking(int fd)
{
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
}
void addfd(int epollfd, int fd, void *ptr, bool nonblock)
{
	epoll_event event;
	memset(&event, 0, sizeof(event));
	event.data.fd = fd;
	event.data.ptr = ptr;
	event.events = EPOLLIN /* | EPOLLET */ | EPOLLRDHUP;
	epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
	if (nonblock)
		setnonblocking(fd);
}
void delfd(int epollfd, int fd)
{
	epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL);
}
void updateEvents(int epollfd, int fd, void *ptr, uint32_t events, int op)
{
	struct epoll_event event;
	memset(&event, 0, sizeof(event));
	event.data.fd = fd;
	event.data.ptr = ptr;
	event.events = events;
	int r = epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
	exit_if(r, "epoll_ctl failed");
}
#elif _WIN32
WSADATA _wsa;
LPFN_ACCEPTEX pAcceptEx;						 // AcceptEx函数指针
LPFN_GETACCEPTEXSOCKADDRS pGetAcceptExSockaddrs; // GetAcceptExSockaddrs函数指针
SOCKET NewClientSocket(SocketType type)
{
	int protocol = type == SocketType::UDP ? SOCK_DGRAM : SOCK_STREAM;
	SOCKET _socket = WSASocket(AF_INET, protocol, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	return _socket;
}
#endif

void RunNetCoreLoop()
{
	if (!NetCoreProcess::Instance()->Running())
	{
		thread CoreThread(&NetCoreProcess::Run, NetCoreProcess::Instance());
		CoreThread.join();
	}
}

void InitNetCore()
{
#ifdef _WIN32
	WSAStartup(MAKEWORD(2, 2), &_wsa);
#endif
}

bool IsHeartBeat(const Buffer &Buffer)
{
	return (Buffer.Length() == 8 && 0 == strncmp((char *)HeartBuffer.Data(), (char *)Buffer.Data(), 8));
}

NetCoreProcess::NetCoreProcess()
{
#ifdef _WIN32
	GUID GuidAcceptEx = WSAID_ACCEPTEX;							// 识别AcceptEx函数的GUID
	GUID GuidGetAcceptExSockaddrs = WSAID_GETACCEPTEXSOCKADDRS; // 识别AcceptEx返回地址信息的函数的GUID
	DWORD dwBytes = 0;

	WSAIoctl(
		WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED),
		SIO_GET_EXTENSION_FUNCTION_POINTER,
		&GuidAcceptEx,
		sizeof(GuidAcceptEx),
		&pAcceptEx,
		sizeof(pAcceptEx),
		&dwBytes,
		NULL,
		NULL);

	WSAIoctl(
		WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED),
		SIO_GET_EXTENSION_FUNCTION_POINTER,
		&GuidGetAcceptExSockaddrs,
		sizeof(GuidGetAcceptExSockaddrs),
		&pGetAcceptExSockaddrs,
		sizeof(pGetAcceptExSockaddrs),
		&dwBytes,
		NULL,
		NULL);

	_HIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, 0);
#endif
}

NetCoreProcess *NetCoreProcess::Instance()
{
	static NetCoreProcess *m_Instance = new NetCoreProcess();
	return m_Instance;
}
int NetCoreProcess::Run()
{
	try
	{
		_isrunning = true;
		// thread HeartLoop(&NetCoreProcess::HeartBeatLoop, this);
		thread EventLoop(&NetCoreProcess::Loop, this);
		// HeartLoop.join();
		EventLoop.join();
		_isrunning = false;

		// ThreadEnd();
		return 1;
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return -1;
	}
}
bool NetCoreProcess::Running()
{
	return _isrunning;
}

#ifdef __linux__
bool NetCoreProcess::AddNetFd(Net *Con)
{
	cout << "AddNetFd fd :" << Con->GetFd() << endl;
	if (Con->GetFd() <= 0)
	{
		return false;
	}
	NetCore_EpollData *data = new NetCore_EpollData();
	data->fd = Con->GetFd();
	data->Con = Con;
	_EpollData.Insert(Con, data);
	_HeartBeatCount.Insert(Con, 0);
	addfd(_epoll, Con->GetFd(), data, true);
	return true;
}
bool NetCoreProcess::DelNetFd(Net *Con)
{
	delfd(_epoll, Con->GetFd());
	cout << "    _HeartBeatCount.Erase(Con);Start\n";
	_HeartBeatCount.Erase(Con);
	cout << "    _HeartBeatCount.Erase(Con);End\n";

	NetCore_EpollData *data = nullptr;
	if (_EpollData.Find(Con, data))
	{
		_EpollData.Erase(Con);
		if (data)
			delete (data);
	}

	return true;
}

void NetCoreProcess::Loop()
{
	// int timefd = timerfd_create(CLOCK_MONOTONIC, 0);
	// itimerspec timer;

	// timeval now;
	// gettimeofday(&now, NULL);
	// timer.it_value.tv_sec =10;
	// timer.it_value.tv_nsec = 0;
	// timer.it_interval.tv_sec = 5;
	// timer.it_interval.tv_nsec = 0;
	// timerfd_settime(timefd, TFD_TIMER_ABSTIME, &timer, NULL);
	// addfd(_epoll, timefd, true);

	cout << "Core , EventLoop\n";

	while (_isrunning)
	{
		int number = epoll_wait(_epoll, _events, 200, -1);
		if (number < 0 && (errno != EINTR))
		{
			cout << "_epoll failure\n";
			break;
		}
		for (int i = 0; i < number; i++)
		{
			EventProcess(_events[i]);
		}
	}
	// close(timefd);
	close(_epoll);
}

int NetCoreProcess::EventProcess(epoll_event &event)
{
	int fd = ((NetCore_EpollData *)event.data.ptr)->fd;
	Net *Con = ((NetCore_EpollData *)event.data.ptr)->Con;
	uint32_t events = event.events;

	/*     if ((event.data.fd == timefd) && (event.events & EPOLLIN))
		{
			cout << "timer out!\n";
			uint64_t exp = 0;
			int ret=read(timefd, &exp, sizeof(uint64_t));
			cout<<exp;
			// addfd(_epoll,timefd,false);
		} */
	/*             if ((((NetCore_EpollData *)event.data.ptr)->fd == _pipe[0]) && (event.events & EPOLLIN))
				{
					int sig;
					char signals[1024];
					int ret = recv(_pipe[0], signals, 1023, 0);
					if (ret == -1 || ret == 0)
						continue;
					else
					{
						for (int i = 0; i < ret; i++)
						{
							switch (signals[i])
							{
							case SIGINT:
							case SIGTERM:
							{
								stop = true;
								break;
							}
							}
						}
					}
				}
				else  */
	if (events & EPOLLRDHUP)
	{
		DelNetFd(Con);
		Con->OnRDHUP();
	}
	else if (events & (EPOLLIN | EPOLLERR))
	{
		// _HeartBeatCount[Con] = 0;
		try
		{
			int oldCount = 0;
			if (!_HeartBeatCount.FindOldAndSetNew(Con, oldCount, 0))
			{
				_HeartBeatCount.Insert(Con, 0);
			}
			Con->OnEPOLLIN(fd);
		}
		catch (const std::exception &e)
		{
			cout << "_HeartBeatCount.Size :" << _HeartBeatCount.Size() << endl;
			std::cerr << e.what() << '\n';
		}
	}
	else if (events & EPOLLOUT)
	{
		if (Con->GetNetType() == NetType::Client)
			SendRes((NetClient *)Con);
	}
	else
	{
		perror("unknown event!");
		// exit_if(1, "unknown event!");
	}

	return 1;
}

bool NetCoreProcess::SendRes(NetClient *Con)
{
	if (!Con->GetSendMtx().try_lock())
		return true; // 写锁正在被其他线程占用
	int fd = Con->GetFd();
	SafeQueue<Package *> &SendDatas = Con->GetSendData();

	int count = 0;
	while (count < 5 && !SendDatas.empty())
	{

		Package *pak = nullptr;
		if (!SendDatas.front(pak))
			break;

		Buffer &buffer = pak->buffer;
		if (!buffer.Data() || buffer.Length() <= 0)
		{
			SendDatas.dequeue(pak);
			SAFE_DELETE(pak);
			count++;
			continue;
		}
		size_t left = buffer.Length() - pak->written;

		int result = 0;
		// 如果有数据没有写完，则一直写数据
		while ((result = ::send(fd, (char *)(buffer.Data()) + pak->written, left, MSG_NOSIGNAL)) > 0)
		{
			if (result <= 0)
			{
				if (result == 0)
				{
					cout << "0000000\n";
				}
				else
				{
					if (errno == EAGAIN || errno == EWOULDBLOCK)
						continue;
					if (errno == EINTR)
						continue;
				}
			}
			pak->written += result;
			left -= result;
		};

		if (left == 0) // 当前包已写完
		{
			SendDatas.dequeue(pak);
			SAFE_DELETE(pak);
			count++;
		}
		else
		{
			if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) // 当前包未写完，但缓冲区已满,或者被系统中断打断
			{

				NetCore_EpollData *data = nullptr;
				if (!_EpollData.Find(Con, data))
				{
					data = new NetCore_EpollData();
					data->fd = Con->GetFd();
					data->Con = Con;
					_EpollData.Insert(Con, data);
				}
				updateEvents(_epoll, fd, data, EPOLLIN | EPOLLOUT | EPOLLRDHUP, EPOLL_CTL_MOD); // 缓冲区已满，则关注其可写事件，等待下次可写事件
				Con->GetSendMtx().unlock();
				return true;
			}
			else if (result <= 0) // Error
			{
				printf("write error for %d: %d %s\n", fd, errno, strerror(errno));
				close(fd);
				this->DelNetFd(Con);
				Con->OnRDHUP();
				Con->GetSendMtx().unlock();
				return false;
			}
		}
	}

	NetCore_EpollData *data = nullptr;
	if (!_EpollData.Find(Con, data))
	{
		data = new NetCore_EpollData();
		data->fd = Con->GetFd();
		data->Con = Con;
		_EpollData.Insert(Con, data);
	}
	if (SendDatas.empty()) // 待发送数据为空,数据已经发送完，不再关注其可写事件
	{
		updateEvents(_epoll, fd, data, EPOLLIN | EPOLLRDHUP, EPOLL_CTL_MOD); // 所有数据发送完毕，不再关注其缓冲区可写事件
	}
	else // 仍有数据未发送,关注其可写事件,等待下次可写事件
	{
		updateEvents(_epoll, fd, data, EPOLLIN | EPOLLOUT | EPOLLRDHUP, EPOLL_CTL_MOD);
	}
	Con->GetSendMtx().unlock();
	return true;
}

#elif _WIN32
bool NetCoreProcess::AddNetFd(Net *Con)
{
	cout << "AddNetFd Socket :" << Con->GetSocket() << endl;
	if (!Con->ValidSocket())
	{
		return false;
	}
	NetCore_SocketData *data = new NetCore_SocketData();
	data->Socket = Con->GetSocket();
	data->Con = Con;
	_SocketData.Insert(Con, data);
	_HeartBeatCount.Insert(Con, 0);

	CreateIoCompletionPort((HANDLE)Con->GetSocket(), _HIOCP, (ULONG_PTR)Con, 0);
	if (Con->GetNetType() == NetType::Client)
	{
		if (!postRecvReq(Con))
		{
			DelNetFd(Con);
			return false;
		}
	}
	if (Con->GetNetType() == NetType::Listener)
	{
		if (!postAcceptReq(Con))
		{
			DelNetFd(Con);
			return false;
		}
	}
	return true;
}
bool NetCoreProcess::DelNetFd(Net *Con)
{
	IODATAMANAGER->CancelIOEvent(Con);
	_HeartBeatCount.Erase(Con);

	NetCore_SocketData *data = nullptr;
	if (_SocketData.Find(Con, data))
	{
		_SocketData.Erase(Con);
		if (data)
			delete (data);
	}

	return true;
}

void NetCoreProcess::Loop()
{
	cout << "Core , EventLoop\n";

	while (_isrunning)
	{
		// int number = epoll_wait(_epoll, _events, 200, -1);
		// if (number < 0 && (errno != EINTR))
		// {
		//     cout << "_epoll failure\n";
		//     break;
		// }
		// for (int i = 0; i < number; i++)
		// {
		//     EventProcess(_events[i]);
		// }

		DWORD dwByteTransferred;
		Net *con = NULL;
		IO_DATA *pIOData = NULL;
		while (true)
		{
			bool bFlag = ::GetQueuedCompletionStatus(_HIOCP, &dwByteTransferred, (PULONG_PTR)&con, (LPOVERLAPPED *)&pIOData, WSA_INFINITE);
			if (!pIOData)
				continue;
			pIOData->NumberOfBytesRecvd = dwByteTransferred;
			EventProcess(pIOData, bFlag);
			IODATAMANAGER->ReleaseData(pIOData);
		}
	}
	// close(timefd);
	CloseHandle(_HIOCP);
}

int NetCoreProcess::EventProcess(IO_DATA *IOData, bool bFlag)
{
	if (!IOData)
		return 0;

	SOCKET socket = IOData->socket;
	Net *Con = IOData->Con;
	DWORD OP_Type = IOData->OP_Type;
	if (!bFlag)
	{
		DelNetFd(Con);
		return 0;
	}
	if (socket == NULL)
	{
		// closesocket(tcp_socket);
		// GlobalFree(pPerIO);
		return -1;
	}

	switch (OP_Type)
	{
	case OP_ACCEPT:
	{
		sockaddr_in *localAddr = NULL;
		sockaddr_in *clientAddr = NULL;
		int remoteLen = sizeof(sockaddr_in), localLen = sizeof(sockaddr_in);
		pGetAcceptExSockaddrs(IOData->buffer.buf, 0, sizeof(sockaddr_in) + 16, sizeof(sockaddr_in) + 16, (LPSOCKADDR *)&localAddr, &localLen, (LPSOCKADDR *)&clientAddr, &remoteLen);

		if (!Con)
			break;
		else
			Con->OnACCEPT(socket, clientAddr);

		if (!postAcceptReq(Con))
		{
			DelNetFd(Con);
			return -1;
		}
	}
	break;

	case OP_READ:
	{

		if (!Con)
			break;
		else
		{
			Buffer buffer(IOData->buffer.buf, IOData->NumberOfBytesRecvd);
			Con->OnREAD(socket, buffer);
		}
		if (!postRecvReq(Con))
		{
			DelNetFd(Con);
			return -1;
		}
	}
	break;

	case OP_WRITE:
	{
		if (!Con)
			break;
		else if (Con->GetSocket() == socket)
		{
			break;
		}
	}
	break;
	default:
		break;
	}
	return 1;
}

bool NetCoreProcess::SendRes(NetClient *Con)
{
	bool result = true;
	if (!Con->GetSendMtx().try_lock())
		return true; // 写锁正在被其他线程占用
	SOCKET socket = Con->GetSocket();
	SafeQueue<Package *> &SendDatas = Con->GetSendData();

	int count = 0;
	while (count < 5 && !SendDatas.empty())
	{

		Package *pak = nullptr;
		if (!SendDatas.front(pak))
			break;

		Buffer &buffer = pak->buffer;
		if (!buffer.Data() || buffer.Length() <= 0)
		{
			SendDatas.dequeue(pak);
			SAFE_DELETE(pak);
			count++;
			continue;
		}
		size_t left = buffer.Length() - pak->written;

		while (left > 0)
		{
			DWORD sendBytes = 0; // 发送字节数
			// 如果有数据没有写完，则一直写数据
			IO_DATA *pIOData = IODATAMANAGER->AllocateData(OP_WRITE, left);
			pIOData->socket = Con->GetSocket();
			pIOData->Con = Con;
			memcpy(pIOData->buffer.buf, (char *)buffer.Data() + pak->written, left);
			if (::WSASend(socket, &pIOData->buffer, 1, &sendBytes, 0, &pIOData->overlap, NULL) == SOCKET_ERROR)
			{
				if (ERROR_IO_PENDING != WSAGetLastError()) // 发起重叠操作失败
				{
					int i = WSAGetLastError();
					cout << WSAGetLastError();
					result = false;
					break;
				}
				result = true;
				break;
			}
			else
			{
				pak->written += sendBytes;
				left -= sendBytes;
			}
		}

		if (left == 0) // 当前包已写完
		{
			SendDatas.dequeue(pak);
			SAFE_DELETE(pak);
			count++;
		}
		else
		{
			if (result == true)
			{
				break;
			}
			else
			{
				DelNetFd(Con);
				// closesocket(socket);
				Con->OnRDHUP();
				break;
			}
		}
	}
	Con->GetSendMtx().unlock();
	return result;
}
#endif

void NetCoreProcess::HeartBeatLoop()
{
	while (_isrunning)
	{

		auto _call = [&](std::map<Net *, int> &map) -> void
		{
			for (auto it = map.begin(); it != map.end();)
			{
				if (it->first->GetNetType() == NetType::Client)
				{
					(it->second)++;
					if ((it->second) >= 6) // 2s*5没有收到心跳包，判定客户端掉线
					{
						NetClient *Con = (NetClient *)it->first;
						if (Con->AsyncSend(HeartBuffer))
						{
							it->second = 0;
							it++;
						}
						else
						{
							it = map.erase(it);
							DelNetFd((Net *)Con);
							Con->OnRDHUP();
						}
						// RateLimiter_Manager::Instance()->Pop(fd);
					}
					else
					{
						it++;
					}
				}
				else
				{
					it++;
				}
			}
		};
		_HeartBeatCount.EnsureCall(_call);
		this_thread::sleep_for(std::chrono::seconds(2)); // 睡眠2秒
	}
}

#ifdef _WIN32
bool NetCoreProcess::postAcceptReq(Net *Con)
{
	IO_DATA *pIOData = IODATAMANAGER->AllocateData(OP_ACCEPT);
	pIOData->Con = Con;
	pIOData->socket = NewClientSocket(Con->GetType());
	int ret = ::pAcceptEx(Con->GetSocket(), pIOData->socket, pIOData->buffer.buf, 0, sizeof(sockaddr_in) + 16, sizeof(sockaddr_in) + 16, nullptr, &pIOData->overlap);
	if (ret != 0)
	{
		IODATAMANAGER->ReleaseData(pIOData);
		int i = WSAGetLastError();
		i++;
	}
	return ret == 0;
}

bool NetCoreProcess::postRecvReq(Net *Con)
{
	IO_DATA *pIOData = IODATAMANAGER->AllocateData(OP_READ);
	pIOData->socket = Con->GetSocket();
	pIOData->Con = Con;
	int ret = ::WSARecv(Con->GetSocket(), &pIOData->buffer, 1, &pIOData->NumberOfBytesRecvd, &pIOData->flag, &pIOData->overlap, NULL);
	if (ret != 0 && ERROR_IO_PENDING != WSAGetLastError())
	{
		IODATAMANAGER->ReleaseData(pIOData);
		int i = WSAGetLastError();
		return false;
	}
	return true;
}
#endif