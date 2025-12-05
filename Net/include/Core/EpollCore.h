#pragma once

#include "Core/DeleteLater.h"
#include "Core/TCPTransportWarpper.h"
#include "CriticalSectionLock.h"
#include "ResourcePool.h"
#include "BiDirectionalMap.h"

class EpollCoreProcess
{
public:
	struct NetCore_EpollData
	{
		int fd;
		BaseTransportConnection *Con;
	};

	// weak包装，防止epoll处理期间EpollData过期
	class EpollDataWeakWrapper
	{
	public:
		EpollDataWeakWrapper(int fd, std::shared_ptr<EpollCoreProcess::NetCore_EpollData> &data);

	public:
		int fd;
		std::weak_ptr<EpollCoreProcess::NetCore_EpollData> weakData;
	};

public:
	static EpollCoreProcess *Instance();
	int Run();
	bool Running();

public:
	bool AddNetFd(BaseTransportConnection *Con);
	bool DelNetFd(BaseTransportConnection *Con);
	bool SendRes(TCPTransportConnection *Con);
	void AddPendingDeletion(DeleteLaterImpl *ptr);

private:
	EpollCoreProcess();
	void Loop();
	int EventProcess(std::shared_ptr<NetCore_EpollData> &data, uint32_t events);
	void ThreadEnd();
	void ProcessPendingDeletions();

private:
	bool _isrunning = false;
	int _epoll = epoll_create(1000);
	epoll_event _events[1500];
	SafeMap<BaseTransportConnection *, std::shared_ptr<NetCore_EpollData>> _EpollData;
	BiDirectionalMap<int, EpollDataWeakWrapper *> _WeakData;
	SafeArray<DeleteLaterImpl *> _pendingDeletions;
};
