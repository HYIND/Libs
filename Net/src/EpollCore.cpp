#include <sys/eventfd.h>
#include <set>
#include "Core/EpollCore.h"
#include "Core/NetCoredef.h"
#include "BiDirectionalMap.h"

static int shutdown_eventfd = -1;

using namespace std;

static void setnonblocking(int fd)
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
	// exit_if(r, "epoll_ctl failed");
}

struct NetCore_EpollData
{
	int fd;
	std::weak_ptr<BaseTransportConnection> Con;
};

// weak包装，防止epoll处理期间EpollData过期
struct EpollDataWeakWrapper
{
	int fd;
	std::weak_ptr<NetCore_EpollData> weakData;

	EpollDataWeakWrapper(int fd, const std::shared_ptr<NetCore_EpollData> &data);
};

class EpollCoreProcessImpl
{
public:
	EpollCoreProcessImpl();
	int Run();
	void Stop();
	bool Running();

public:
	bool AddNetFd(std::shared_ptr<BaseTransportConnection> Con);
	bool DelNetFd(BaseTransportConnection *Con);
	bool SendRes(std::shared_ptr<BaseTransportConnection> BaseCon);
	void AddPendingDeletion(DeleteLaterImpl *ptr);

private:
	void Loop();
	int EventProcess(std::shared_ptr<NetCore_EpollData> &data, uint32_t events);
	void ThreadEnd();
	void ProcessPendingDeletions();

private:
	bool _isrunning;
	int _epoll = epoll_create(1000);
	epoll_event _events[1500];
	SafeMap<BaseTransportConnection *, std::shared_ptr<NetCore_EpollData>> _EpollData;
	BiDirectionalMap<int, EpollDataWeakWrapper *> _WeakData;
	SafeArray<DeleteLaterImpl *> _pendingDeletions;
};

EpollDataWeakWrapper::EpollDataWeakWrapper(int fd, const std::shared_ptr<NetCore_EpollData> &data)
	: fd(fd), weakData(data)
{
}

EpollCoreProcessImpl::EpollCoreProcessImpl()
	: _isrunning(false), _epoll(-1)
{
	if (shutdown_eventfd < 0)
	{
		shutdown_eventfd = eventfd(0, EFD_NONBLOCK);
		if (shutdown_eventfd < 0)
		{
			perror("shutdown_eventfd init error!");
		}
	}
	_epoll = epoll_create(1000);
	if (_epoll < 0)
	{
		perror("epollfd init error!");
	}
	if (shutdown_eventfd > 0 && _epoll > 0)
	{
		EpollDataWeakWrapper *wrapper = new EpollDataWeakWrapper(shutdown_eventfd, std::shared_ptr<NetCore_EpollData>(nullptr));
		addfd(_epoll, shutdown_eventfd, wrapper, true);
	}
}

int EpollCoreProcessImpl::Run()
{
	try
	{
		if (_epoll < 0)
		{
			std::cerr << "invalid epollfd!\n";
			return -1;
		}
		if (shutdown_eventfd > 0 && _epoll > 0)
		{
			uint64_t value = 0;
			ssize_t n = read(shutdown_eventfd, &value, sizeof(value));
		}

		_isrunning = true;
		thread EventLoop(&EpollCoreProcessImpl::Loop, this);
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

void EpollCoreProcessImpl::Stop()
{
	if (shutdown_eventfd < 0)
		return;
	uint64_t num = 1;
	write(shutdown_eventfd, &num, sizeof(uint64_t));
}

bool EpollCoreProcessImpl::Running()
{
	return _isrunning;
}

bool EpollCoreProcessImpl::AddNetFd(std::shared_ptr<BaseTransportConnection> Con)
{
	// cout << "AddNetFd fd :" << Con->GetFd() << endl;
	if (Con->GetFd() <= 0)
	{
		return false;
	}
	std::shared_ptr<NetCore_EpollData> data = std::make_shared<NetCore_EpollData>();
	data->fd = Con->GetFd();
	data->Con = Con;
	_EpollData.Insert(Con.get(), data);

	EpollDataWeakWrapper *weak = nullptr;
	if (_WeakData.FindByLeft(Con->GetFd(), weak))
	{
		_WeakData.EraseByLeft(Con->GetFd());
		SAFE_DELETE(weak);
	}
	auto new_weak = new EpollDataWeakWrapper(Con->GetFd(), data);
	_WeakData.Insert(Con->GetFd(), new_weak);
	addfd(_epoll, Con->GetFd(), new_weak, true);

	return true;
}
bool EpollCoreProcessImpl::DelNetFd(BaseTransportConnection *Con)
{
	delfd(_epoll, Con->GetFd());
	_EpollData.Erase(Con);
	_WeakData.EraseByLeft(Con->GetFd());
	return true;
}

void EpollCoreProcessImpl::Loop()
{
	cout << "EpollCore , EventLoop\n";

	bool shouldshutdown = false;
	while (_isrunning)
	{
		if (shouldshutdown)
			break;

		static int maxevents = 1000;
		epoll_event _events[maxevents];
		int number = epoll_wait(_epoll, _events, maxevents, 100);
		if (number < 0 && (errno != EINTR))
		{
			cout << "_epoll failure\n";
			break;
		}

		for (int i = 0; i < number; i++)
		{
			try
			{
				epoll_event event = _events[i];
				auto *wrapper = static_cast<EpollDataWeakWrapper *>(event.data.ptr);
				if (wrapper)
				{
					if (shutdown_eventfd > 0 && wrapper->fd == shutdown_eventfd)
					{
						uint64_t num;
						read(shutdown_eventfd, &num, sizeof(uint64_t));
						shouldshutdown = true;
						std::cout << "EpollCore , EventLoop closing......\n";
						continue;
					}

					int weakfd = -1;
					if (_WeakData.FindByRight(wrapper, weakfd))
					{
						auto data = wrapper->weakData.lock();
						if (data)
							EventProcess(data, event.events);
						else
						{
							_WeakData.EraseByRight(wrapper);
							delfd(_epoll, weakfd);
						}
					}
				}
			}
			catch (const std::exception &e)
			{
				std::cerr << "EventLoop unknown exception:" << e.what() << '\n';
			}
		}
		ProcessPendingDeletions();
	}
}

int EpollCoreProcessImpl::EventProcess(std::shared_ptr<NetCore_EpollData> &data, uint32_t events)
{
	if (!data)
		return -1;

	int fd = data->fd;
	auto Con = data->Con.lock();
	if (!Con)
	{
		return -1;
	}
	if (fd <= 0 || !Con->ValidSocket())
		return -1;

	if (events & EPOLLRDHUP)
	{
		DelNetFd(Con.get());
		Con->RDHUP();
		return 0;
	}
	else
	{
		if (events & (EPOLLIN | EPOLLERR))
		{
			try
			{
				Con->READ(fd);
			}
			catch (const std::exception &e)
			{
				std::cerr << e.what() << '\n';
			}
		}
		else if (events & EPOLLOUT)
		{
			if (Con->GetNetType() == NetType::Client)
				SendRes(Con);
		}
		else
		{
			perror("unknown event!");
			// exit_if(1, "unknown event!");
		}
	}

	return 1;
}

void EpollCoreProcessImpl::AddPendingDeletion(DeleteLaterImpl *ptr)
{
	_pendingDeletions.emplace(ptr);
}

void EpollCoreProcessImpl::ProcessPendingDeletions()
{
	std::vector<DeleteLaterImpl *> deletions;
	_pendingDeletions.EnsureCall(
		[&](std::vector<DeleteLaterImpl *> &array) -> void
		{
			deletions.swap(array); // 取出待删除任务
		}

	);
	for (auto ptr : deletions)
	{
		SAFE_DELETE(ptr);
	}
}

bool EpollCoreProcessImpl::SendRes(std::shared_ptr<BaseTransportConnection> BaseCon)
{
	if (!BaseCon || !(BaseCon->GetNetType() == NetType::Client))
		return false;

	std::shared_ptr<TCPTransportConnection> Con = BaseCon->GetShared<TCPTransportConnection>();
	if (!Con)
		return false;

	if (!Con->GetSendMtx().TryEnter())
		return true; // 写锁正在被其他线程占用
	int fd = Con->GetFd();
	SafeQueue<Buffer *> &SendDatas = Con->GetSendData();

	std::shared_ptr<NetCore_EpollData> data;
	if (!_EpollData.Find(Con.get(), data))
	{
		if (!AddNetFd(Con))
			return false;
	}

	EpollDataWeakWrapper *wrapper = nullptr;
	if (!_WeakData.FindByLeft(fd, wrapper))
		return false;

	int count = 0;
	while (count < 5 && !SendDatas.empty())
	{

		Buffer *buffer = nullptr;
		if (!SendDatas.front(buffer))
			break;

		if (!buffer->Data() || buffer->Length() <= 0)
		{
			SendDatas.dequeue(buffer);
			SAFE_DELETE(buffer);
			count++;
			continue;
		}
		size_t left = buffer->Length() - buffer->Position();

		int result = 0;
		// 如果有数据没有写完，则一直写数据
		while ((result = ::send(fd, (char *)(buffer->Data()) + buffer->Position(), left, MSG_NOSIGNAL)) > 0)
		{
			if (result <= 0)
			{
				if (result == 0)
				{
					// cout << "0000000\n";
				}
				else
				{
					if (errno == EAGAIN || errno == EWOULDBLOCK)
						continue;
					if (errno == EINTR)
						continue;
				}
			}
			buffer->Seek(buffer->Position() + result);
			left -= result;
		};

		if (left == 0) // 当前包已写完
		{
			SendDatas.dequeue(buffer);
			SAFE_DELETE(buffer);
			count++;
		}
		else
		{
			if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) // 当前包未写完，但缓冲区已满,或者被系统中断打断
			{
				updateEvents(_epoll, fd, wrapper, EPOLLIN | EPOLLOUT | EPOLLRDHUP, EPOLL_CTL_MOD); // 缓冲区已满，则关注其可写事件，等待下次可写事件
				Con->GetSendMtx().unlock();
				return true;
			}
			else if (result <= 0) // Error
			{
				printf("write error for %d: %d %s\n", fd, errno, strerror(errno));
				close(fd);
				this->DelNetFd(Con.get());
				Con->RDHUP();
				Con->GetSendMtx().unlock();
				return false;
			}
		}
	}

	if (SendDatas.empty()) // 待发送数据为空,数据已经发送完，不再关注其可写事件
	{
		updateEvents(_epoll, fd, wrapper, EPOLLIN | EPOLLRDHUP, EPOLL_CTL_MOD); // 所有数据发送完毕，不再关注其缓冲区可写事件
	}
	else // 仍有数据未发送,关注其可写事件,等待下次可写事件
	{
		updateEvents(_epoll, fd, wrapper, EPOLLIN | EPOLLOUT | EPOLLRDHUP, EPOLL_CTL_MOD);
	}

	Con->GetSendMtx().unlock();
	return true;
}

EpollCoreProcess *EpollCoreProcess::Instance()
{
	static EpollCoreProcess *m_instance = new EpollCoreProcess();
	return m_instance;
}
EpollCoreProcess::EpollCoreProcess()
{
	pImpl = std::make_unique<EpollCoreProcessImpl>();
}
int EpollCoreProcess::Run() { return pImpl->Run(); }
void EpollCoreProcess::Stop() { pImpl->Stop(); };
bool EpollCoreProcess::Running() { return pImpl->Running(); };
bool EpollCoreProcess::AddNetFd(std::shared_ptr<BaseTransportConnection> Con) { return pImpl->AddNetFd(Con); };
bool EpollCoreProcess::DelNetFd(BaseTransportConnection *Con) { return pImpl->DelNetFd(Con); };
bool EpollCoreProcess::SendRes(std::shared_ptr<BaseTransportConnection> BaseCon) { return pImpl->SendRes(BaseCon); };
void EpollCoreProcess::AddPendingDeletion(DeleteLaterImpl *ptr) { pImpl->AddPendingDeletion(ptr); };