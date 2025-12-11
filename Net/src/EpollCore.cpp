#include "Core/EpollCore.h"
#include "Core/NetCoredef.h"
#include <set>

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

EpollCoreProcess::EpollDataWeakWrapper::EpollDataWeakWrapper(int fd, std::shared_ptr<EpollCoreProcess::NetCore_EpollData> &data)
	: fd(fd), weakData(data)
{
}

EpollCoreProcess::EpollCoreProcess()
{
}

EpollCoreProcess *EpollCoreProcess::Instance()
{
	static EpollCoreProcess *m_Instance = new EpollCoreProcess();
	return m_Instance;
}

int EpollCoreProcess::Run()
{
	try
	{
		_isrunning = true;
		thread EventLoop(&EpollCoreProcess::Loop, this);
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

void EpollCoreProcess::Stop()
{
	_isrunning = false;
}

bool EpollCoreProcess::Running()
{
	return _isrunning;
}

bool EpollCoreProcess::AddNetFd(std::shared_ptr<BaseTransportConnection> Con)
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
bool EpollCoreProcess::DelNetFd(BaseTransportConnection *Con)
{
	delfd(_epoll, Con->GetFd());
	_EpollData.Erase(Con);
	_WeakData.EraseByLeft(Con->GetFd());
	return true;
}

void EpollCoreProcess::Loop()
{
	cout << "EpollCore , EventLoop\n";

	while (_isrunning)
	{
		int number = epoll_wait(_epoll, _events, 800, 100);
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
	// close(timefd);
	close(_epoll);
}

int EpollCoreProcess::EventProcess(std::shared_ptr<NetCore_EpollData> &data, uint32_t events)
{
	if (!data)
		return -1;

	int fd = data->fd;
	auto Con = data->Con.lock();
	if (!Con)
	{
		return;
	}
	if (fd <= 0 || !Con->ValidSocket())
		return -1;

	if (events & EPOLLRDHUP)
	{
		DelNetFd(Con.get());
		Con->OnRDHUP();
		return 0;
	}
	else
	{
		if (events & (EPOLLIN | EPOLLERR))
		{
			try
			{
				Con->OnREAD(fd);
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

void EpollCoreProcess::AddPendingDeletion(DeleteLaterImpl *ptr)
{
	_pendingDeletions.emplace(ptr);
}

void EpollCoreProcess::ProcessPendingDeletions()
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

bool EpollCoreProcess::SendRes(std::shared_ptr<BaseTransportConnection> BaseCon)
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
		size_t left = buffer->Length() - buffer->Postion();

		int result = 0;
		// 如果有数据没有写完，则一直写数据
		while ((result = ::send(fd, (char *)(buffer->Data()) + buffer->Postion(), left, MSG_NOSIGNAL)) > 0)
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
			buffer->Seek(buffer->Postion() + result);
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
				Con->OnRDHUP();
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
