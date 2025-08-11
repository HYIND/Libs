/*
	该文件是对IO事件驱动的封装，用于触发网络事件，进行数据的收发
	NetCoreProcess是一个网络事件驱动核心，在Linux下是基于epoll封装，在Windows中是基于HIOCP进行封装
*/

#pragma once

#include "Core/DeleteLater.h"
#include "TCPTransportWarpper.h"
#include "ResourcePool.h"

#ifdef __linux__
struct NetCore_EpollData
{
	int fd;
	BaseTransportConnection *Con;
};
#elif _WIN32
struct NetCore_SocketData
{
	SOCKET Socket;
	Net *Con;
};

// hIOCP 重叠结构体
typedef struct _PER_IO_DATA
{
	OVERLAPPED overlap; // 重叠结构
	SOCKET socket;
	DWORD NumberOfBytesRecvd;
	DWORD OP_Type;
	DWORD flag;
	Net *Con;
	WSABUF buffer;

#define OP_READ 1
#define OP_WRITE 2
#define OP_ACCEPT 3

	_PER_IO_DATA(DWORD OP_Type = OP_READ, int bufSize = 1024) : OP_Type(OP_Type)
	{
		buffer.buf = new char[bufSize]{0};
		buffer.len = bufSize;
		NumberOfBytesRecvd = 0;
		flag = 0;
		ZeroMemory(&overlap, sizeof(overlap));
		// overlap.hEvent = NULL; // 完成端口中不需要事件，置空
	}
	~_PER_IO_DATA()
	{
		SAFE_DELETE_ARRAY(buffer.buf);
		buffer.len = 0;
	}
	void Reset()
	{
		ZeroMemory(&overlap, sizeof(overlap));
		socket = INVALID_SOCKET;
		NumberOfBytesRecvd = 0;
		flag = 0;
		Con = nullptr;
	}
	void ReSizeBuffer(int bufSize = 1024)
	{
		if (buffer.len != bufSize)
		{
			SAFE_DELETE_ARRAY(buffer.buf);
			buffer.buf = new char[bufSize]{0};
			buffer.len = bufSize;
		}
		else
		{
			memset(buffer.buf, '0', buffer.len);
		}
	}
} IO_DATA;

class IODataManager : public ResPool<IO_DATA>
{
public:
	static IODataManager *Instance()
	{
		static IODataManager *Instance = new IODataManager();
		return Instance;
	}
	virtual void ResetData(IO_DATA *data)
	{
		data->Reset();
	}
	virtual IO_DATA *AllocateData(DWORD OP_Type, int bufSize = 1024)
	{
		IO_DATA *data = ResPool<IO_DATA>::AllocateData();
		data->OP_Type = OP_Type;
		data->ReSizeBuffer(bufSize);
		return data;
	}
	void CancelIOEvent(Net *Con)
	{
		GETRESLOCK;
		for (auto it = _datas.begin(); it != _datas.end();)
		{
			IO_DATA *data = *it;
			if (data->Con == Con)
			{
				data->overlap.hEvent = (HANDLE)1;
				delete data;
				it = _datas.erase(it);
			}
			else
			{
				it++;
			}
		}
		GETRESUNLOCK;
	}
};

#define IODATAMANAGER IODataManager::Instance()
#endif

class NetCoreProcess
{

public:
	static NetCoreProcess *Instance();
	int Run();
	bool Running();

public:
	bool AddNetFd(BaseTransportConnection *Con);
	bool DelNetFd(BaseTransportConnection *Con);
	bool SendRes(TCPTransportConnection *Con);
	void AddPendingDeletion(DeleteLaterImpl* ptr);

private:
	NetCoreProcess();
	void Loop();
#ifdef __linux__
	int EventProcess(epoll_event &event);
#elif _WIN32
	int EventProcess(IO_DATA *event, bool bFlag);
#endif
	void ThreadEnd();
	void ProcessPendingDeletions();

#ifdef _WIN32
private:
	bool postAcceptReq(Net *Con);
	bool postRecvReq(Net *Con);
#endif

private:
	bool _isrunning = false;
#ifdef __linux__
	int _epoll = epoll_create(1000);
	epoll_event _events[1500];
	SafeMap<BaseTransportConnection *, NetCore_EpollData *> _EpollData;
#elif _WIN32
	HANDLE _HIOCP;
	SafeMap<Net *, NetCore_SocketData *> _SocketData;
#endif

    SafeArray<DeleteLaterImpl* > _pendingDeletions;
    std::mutex _deletionMutex;
};

bool IsHeartBeat(const Buffer &buf);

#define NetCore NetCoreProcess::Instance()
