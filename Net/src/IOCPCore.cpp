
#include "Core/NetCoredef.h"
#include "Core/IOCPCore.h"
#include "Core/BaseSocket.h"

#include "ResourcePool.h"
#include "ThreadPool.h"

#include "Session/SessionListener.h"
#include "Session/BaseNetWorkSession.h"
#include "EndPoint/TCPEndPoint.h"
#include "EndPoint/TcpEndPointListener.h"

#include "BiDirectionalMap.h"

#define RECVBUFFERMINLEN 1024
#define RECVBUFFERDEFLEN 1024 * 5
#define RECVBUFFERMAXLEN 1024 * 1024

#define SENDBUFFERCONCATMAXLEN 1024 * 1024
#define RECVBUFFERCONCATMAXLEN 1024 * 1024

constexpr BaseSocket shutdown_eventfd = 0xFFFFFFFF;

constexpr int addrBufSize = sizeof(SOCKADDR_STORAGE) + 16;

bool CreateIOCP(HANDLE& handle) {
	HANDLE iocp = CreateIoCompletionPort(
		INVALID_HANDLE_VALUE,
		NULL,
		0,
		0
	);

	if (iocp != NULL)
	{
		handle = iocp;
		return true;
	}

	return false;
}

enum class IOCP_OPType
{
	OP_READ = 1,
	OP_WRITE = 2,
	OP_ACCEPT = 3,
	OP_WILLWRITE = 4,
	OP_SHUTDOWN
};
struct IOCPOPData
{
	OVERLAPPED overlapped;

	IOCP_OPType OP_Type;
	BaseSocket fd;
	std::weak_ptr<BaseTransportConnection> weakCon;
	BaseTransportConnection* raw_ptr;

	Buffer buffer;
	WSABUF wsaBuf;

	BaseSocket client_fd;
	sockaddr_in client_addr; // 客户端地址

	DWORD res; // IO操作完成后，通知前写入结果

	IOCPOPData()
		: IOCPOPData(IOCP_OPType::OP_READ)
	{
	}
	IOCPOPData(IOCP_OPType OP_Type)
		: OP_Type(OP_Type)
	{
		memset(&overlapped, 0, sizeof(OVERLAPPED));
		fd = 0;
		raw_ptr = nullptr;
		wsaBuf.buf = NULL;
		wsaBuf.len = 0;
		client_fd = 0;
		memset(&client_addr, '\0', sizeof(sockaddr_in));
		res = 0;
	}
	~IOCPOPData()
	{
		Reset();
	}
	void Reset()
	{
		memset(&overlapped, 0, sizeof(OVERLAPPED));
		OP_Type = IOCP_OPType::OP_READ;
		fd = 0;
		weakCon.reset();
		raw_ptr = nullptr;
		buffer.Release();
		wsaBuf.buf = NULL;
		wsaBuf.len = 0;
		client_fd = 0;
		memset(&client_addr, '\0', sizeof(sockaddr_in));
		res = 0;
	}

	static IOCPOPData* CreateWriteOP(std::shared_ptr<BaseTransportConnection> Con, Buffer& buf);
	static IOCPOPData* CreateReadOP(std::shared_ptr<BaseTransportConnection> Con, uint32_t size);
	static IOCPOPData* CreateAcceptOP(std::shared_ptr<BaseTransportConnection> Con);
	static IOCPOPData* CreateWillWriteOP(std::shared_ptr<BaseTransportConnection> Con);
};

class IOCPCoreProcessImpl
{

	class SequentialIOSubmitter;
	class SequentialEventExecutor;
	class DynamicBufferState;

private:
	struct NetCore_IOCPData
	{
		BaseSocket fd;
		std::weak_ptr<BaseTransportConnection> weakCon;
		std::vector<std::shared_ptr<SequentialIOSubmitter>> senders; // IO流水线
		std::shared_ptr<SequentialEventExecutor> recver;             // 事件处理流水线（包含ACCEPT）
		std::shared_ptr<DynamicBufferState> state;

		bool SubmitIOEvent(IOCPOPData* opdata);
		void GetPostIOEvent(std::vector<IOCPOPData*>& out);
		void NotifyIOEventDone(IOCPOPData* opdata);
		void NotifyIOEventRetry(IOCPOPData* opdata);
	};

	class SequentialIOSubmitter : public std::enable_shared_from_this<SequentialIOSubmitter>
	{
		enum class submitstate
		{
			none = -1,
			idle = 0,
			doing = 1
		};

	public:
		SequentialIOSubmitter(IOCPCoreProcessImpl* core, std::shared_ptr<NetCore_IOCPData>& data, IOCP_OPType type);
		~SequentialIOSubmitter();
		void Release();
		void SubmitOPdata(IOCPOPData* opdata);
		void NotifyRetry(IOCPOPData* opdata);
		void NotifyDone(IOCPOPData* opdata);
		IOCP_OPType GetSubmitType();
		void GetPostIOEvent(std::vector<IOCPOPData*>& out);

	private:
		IOCPCoreProcessImpl* _core;
		std::weak_ptr<NetCore_IOCPData> _weakdata;
		IOCP_OPType _type;

		CriticalSectionLock _lock;
		SafeDeQue<IOCPOPData*> _queue;
		submitstate _state;
	};

	class SequentialEventExecutor
	{
		enum class excutestate;

	public:
		struct ExcuteEvent;

	public:
		SequentialEventExecutor(IOCPCoreProcessImpl* _core, std::shared_ptr<NetCore_IOCPData>& data);
		~SequentialEventExecutor();
		void Release();
		void SubmitExcuteEvent(std::shared_ptr<ExcuteEvent> event);
		void NotifyDone();
		void GetPostExcuteEvent(std::vector<std::shared_ptr<ExcuteEvent>>& out);
		bool Active();
		void SetActive(bool value);

	private:
		IOCPCoreProcessImpl* _core;
		std::weak_ptr<NetCore_IOCPData> _weakdata;
		CriticalSectionLock _lock;
		SafeQueue<std::shared_ptr<ExcuteEvent>> _queue;
		excutestate _state;
		bool _active;
	};

	// 动态缓冲区管理,用于动态调整连接的缓冲区以适应突发的流量
	class DynamicBufferState
	{
	public:
		DynamicBufferState();
		void Update(uint32_t newbufferrecvlen);
		uint32_t GetDynamicSize();

	private:
		uint32_t lastbuffersize;
	};

public:
	IOCPCoreProcessImpl();
	~IOCPCoreProcessImpl();
	int Run();
	void Stop();
	bool Running();

public:
	bool AddNetFd(std::shared_ptr<BaseTransportConnection> Con);
	bool DelNetFd(BaseTransportConnection* Con);
	bool SendRes(std::shared_ptr<BaseTransportConnection> BaseCon);
	void AddPendingDeletion(DeleteLaterImpl* ptr);

private:
	void LoopSubmitIOEvent();
	void LoopSubmitExcuteEvent();
	void Loop();
	bool GetDoneIOEvents(std::vector<IOCPOPData*>& opdatas);
	int EventProcess(IOCPOPData* opdata, std::vector<IOCPOPData*>& postOps);
	void ProcessPendingDeletions();

private:
	void DoPostIOEvents(std::vector<IOCPOPData*> opdatas);
	void DoPostExcuteEvents(std::vector<std::shared_ptr<SequentialEventExecutor::ExcuteEvent>>& events);

	bool SubmitWriteEvent(IOCPOPData* opdata);
	bool SubmitReadEvent(IOCPOPData* opdata);
	bool SubmitAcceptEvent(IOCPOPData* opdata);
	bool SubmitWillWriteEvent(IOCPOPData* opdata);

	bool AssociateSocketWithIOCP(SOCKET socket, ULONG_PTR completionKey, BaseTransportConnection* Con = nullptr);

private:
	bool _shouldshutdown;
	bool _isrunning;
	bool _isinitsuccess;

	HANDLE _iocp;

	SafeMap<BaseTransportConnection*, std::shared_ptr<NetCore_IOCPData>> _IOCPData;
	SafeArray<DeleteLaterImpl*> _pendingDeletions;
	ThreadPool _ExcuteEventProcessPool;

	CriticalSectionLock _IOEventLock;
	ConditionVariable _IOEventCV;

	CriticalSectionLock _ExcuteLock;
	ConditionVariable _ExcuteCV;

	CriticalSectionLock _doPostIOEventLock;

	CriticalSectionLock _associateSocketLock;
	SafeMap<BaseSocket, BaseTransportConnection*> _associateSocketMap;
};

int64_t GetTimestampMilliseconds()
{
	auto now = std::chrono::system_clock::now();
	auto duration = now.time_since_epoch();
	return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

enum class IOCPCoreProcessImpl::SequentialEventExecutor::excutestate
{
	none = -1,
	idle = 0,
	doing = 1
};
struct IOCPCoreProcessImpl::SequentialEventExecutor::ExcuteEvent
{
	enum class EventType
	{
		READ_DATA,
		ACCEPT_CONNECTION,
		READ_HUP
	};
	EventType type;
	std::weak_ptr<NetCore_IOCPData> weakdata;
	std::shared_ptr<Buffer> data;
	struct
	{
		int client_fd;
		sockaddr_in client_addr;
	} accept_info;

	ExcuteEvent(EventType type, std::shared_ptr<NetCore_IOCPData> iodata) : type(type), weakdata(iodata) {}
	~ExcuteEvent()
	{
		if (data)
			data->Release();
	}

	static std::shared_ptr<ExcuteEvent> CreateReadEvent(std::shared_ptr<NetCore_IOCPData> iodata, Buffer& buf);
	static std::shared_ptr<ExcuteEvent> CreateAcceptEvent(std::shared_ptr<NetCore_IOCPData> iodata, int client_fd, sockaddr_in client_addr);
	static std::shared_ptr<ExcuteEvent> CreateRDHUP(std::shared_ptr<NetCore_IOCPData> iodata);
};

class IODataManager : public ResPool<IOCPOPData>
{
public:
	static IODataManager* Instance()
	{
		static IODataManager* Instance = new IODataManager();
		return Instance;
	}
	IODataManager() : ResPool(200, 1000)
	{
	}
	void ResetData(IOCPOPData* data)
	{
		data->Reset();
	}
	IOCPOPData* AllocateData(IOCP_OPType OP_Type, int fd, std::shared_ptr<BaseTransportConnection> Con, uint32_t buffersize = RECVBUFFERDEFLEN)
	{
		IOCPOPData* data = ResPool<IOCPOPData>::AllocateData();
		data->OP_Type = OP_Type;
		data->fd = fd;
		data->weakCon = Con;
		data->raw_ptr = Con.get();
		if (OP_Type == IOCP_OPType::OP_READ || OP_Type == IOCP_OPType::OP_WRITE || OP_Type == IOCP_OPType::OP_SHUTDOWN)
		{
			data->buffer.ReSize(buffersize);
		}
		else if (OP_Type == IOCP_OPType::OP_ACCEPT)
		{
			memset(&data->client_addr, 0, sizeof(sockaddr_in));
		}
		return data;
	}
};
#define IODATAMANAGER IODataManager::Instance()

IOCPOPData* IOCPOPData::CreateWriteOP(std::shared_ptr<BaseTransportConnection> Con, Buffer& buf)
{
	IOCPOPData* data = IODATAMANAGER->AllocateData(IOCP_OPType::OP_WRITE, Con->GetSocket(), Con, 0);
	int oripos = buf.Position();
	data->buffer.QuoteFromBuf(buf);
	data->buffer.Seek(oripos);
	return data;
}
IOCPOPData* IOCPOPData::CreateReadOP(std::shared_ptr<BaseTransportConnection> Con, uint32_t size)
{
	IOCPOPData* data = IODATAMANAGER->AllocateData(IOCP_OPType::OP_READ, Con->GetSocket(), Con, size);
	return data;
}

IOCPOPData* IOCPOPData::CreateAcceptOP(std::shared_ptr<BaseTransportConnection> Con)
{
	IOCPOPData* data = IODATAMANAGER->AllocateData(IOCP_OPType::OP_ACCEPT, Con->GetSocket(), Con, 0);
	return data;
}
IOCPOPData* IOCPOPData::CreateWillWriteOP(std::shared_ptr<BaseTransportConnection> Con)
{
	IOCPOPData* data = IODATAMANAGER->AllocateData(IOCP_OPType::OP_WILLWRITE, Con->GetSocket(), Con, 0);
	return data;
}

bool IOCPCoreProcessImpl::NetCore_IOCPData::SubmitIOEvent(IOCPOPData* opdata)
{
	if (!opdata)
		return false;
	for (auto sender : senders)
	{
		if (sender->GetSubmitType() == opdata->OP_Type)
		{
			sender->SubmitOPdata(opdata);
			return true;
		}
	}
	return false;
}

void IOCPCoreProcessImpl::NetCore_IOCPData::GetPostIOEvent(std::vector<IOCPOPData*>& out)
{
	for (auto sender : senders)
	{
		if (sender)
			sender->GetPostIOEvent(out);
	}
}

void IOCPCoreProcessImpl::NetCore_IOCPData::NotifyIOEventDone(IOCPOPData* opdata)
{
	for (auto& sender : senders)
	{
		if (sender->GetSubmitType() == opdata->OP_Type)
		{
			sender->NotifyDone(opdata);
			return;
		}
	}
}
void IOCPCoreProcessImpl::NetCore_IOCPData::NotifyIOEventRetry(IOCPOPData* opdata)
{
	for (auto& sender : senders)
	{
		if (sender->GetSubmitType() == opdata->OP_Type)
		{
			sender->NotifyRetry(opdata);
			return;
		}
	}
}

IOCPCoreProcessImpl::SequentialIOSubmitter::SequentialIOSubmitter(
	IOCPCoreProcessImpl* core,
	std::shared_ptr<NetCore_IOCPData>& data,
	IOCP_OPType type)
	: _core(core), _weakdata(data), _type(type), _state(submitstate::idle)
{
}

IOCPCoreProcessImpl::SequentialIOSubmitter::~SequentialIOSubmitter()
{
	Release();
}

void IOCPCoreProcessImpl::SequentialIOSubmitter::Release()
{
	_state = submitstate::none;

	std::lock_guard<CriticalSectionLock> lock(_lock);
	while (!_queue.empty())
	{
		IOCPOPData* opdata = nullptr;
		if (_queue.dequeue_front(opdata) && opdata)
			IODATAMANAGER->ReleaseData(opdata);
	}
}

void IOCPCoreProcessImpl::SequentialIOSubmitter::SubmitOPdata(IOCPOPData* opdata)
{
	if (!_core)
		return;

	if (!opdata || opdata->OP_Type != _type)
		return;

	if (_state == submitstate::none)
		return;

	std::lock_guard<CriticalSectionLock> lock(_lock);
	if (_state == submitstate::none || !_weakdata.lock())
		return;

	_queue.enqueue_back(opdata);

	if (_state == submitstate::idle)
	{
		_core->_IOEventCV.NotifyOne();
	}
}

void IOCPCoreProcessImpl::SequentialIOSubmitter::NotifyDone(IOCPOPData* opdata)
{
	if (_state == submitstate::none)
		return;

	if (opdata->OP_Type != _type)
		return;

	std::lock_guard<CriticalSectionLock> lock(_lock);
	if (_state == submitstate::none)
		return;

	if (_type == IOCP_OPType::OP_WRITE)
	{
		if (opdata->buffer.Remain())
		{
			// 上次传输未完全完成，创建新的IOCPOPData任务，然后放到队首，继续传输
			if (auto iodata = _weakdata.lock())
			{
				if (auto Con = iodata->weakCon.lock())
				{
					auto submitopdata = IOCPOPData::CreateWriteOP(Con, opdata->buffer);
					_queue.enqueue_front(submitopdata);
				}
			}
		}
	}
	_state = submitstate::idle;
	if (!_queue.empty())
		_core->_IOEventCV.NotifyOne();
}

void IOCPCoreProcessImpl::SequentialIOSubmitter::NotifyRetry(IOCPOPData* opdata)
{
	std::lock_guard<CriticalSectionLock> lock(_lock);

	if (auto iodata = _weakdata.lock())
	{
		if (auto Con = iodata->weakCon.lock())
		{
			switch (opdata->OP_Type)
			{
			case IOCP_OPType::OP_WRITE:
			{
				auto submitopdata = IOCPOPData::CreateWriteOP(Con, opdata->buffer);
				_queue.enqueue_front(submitopdata);
			}
			break;
			case IOCP_OPType::OP_READ:
			{
				auto submitopdata = IOCPOPData::CreateReadOP(Con, opdata->buffer.Length());
				_queue.enqueue_front(submitopdata);
			}
			break;
			case IOCP_OPType::OP_ACCEPT:
			{
				auto submitopdata = IOCPOPData::CreateAcceptOP(Con);
				_queue.enqueue_front(submitopdata);
			}
			break;
			case IOCP_OPType::OP_WILLWRITE:
			{
				auto submitopdata = IOCPOPData::CreateWillWriteOP(Con);
				_queue.enqueue_front(submitopdata);
			}
			default:
				break;
			}
		}
	}

	_state = submitstate::idle;
	if (!_queue.empty())
		_core->_IOEventCV.NotifyOne();
}

IOCP_OPType IOCPCoreProcessImpl::SequentialIOSubmitter::GetSubmitType()
{
	return _type;
}

void IOCPCoreProcessImpl::SequentialIOSubmitter::GetPostIOEvent(std::vector<IOCPOPData*>& out)
{
	if (_state != submitstate::idle)
		return;

	std::lock_guard<CriticalSectionLock> lock(_lock);
	if (_state != submitstate::idle)
		return;

	if (!_queue.empty())
	{
		if (_type != IOCP_OPType::OP_WRITE)
		{
			IOCPOPData* opdata;
			if (_queue.dequeue_front(opdata) && opdata)
			{
				out.emplace_back(opdata);
				_state = submitstate::doing;
			}
		}
		else // 尝试合并写事件
		{
			IOCPOPData* fistdata;
			if (_queue.dequeue_front(fistdata) && fistdata)
			{
				// 最多合并9个
				int count = 9;
				while (!_queue.empty() && count > 0)
				{
					IOCPOPData* opdata = nullptr;
					if (_queue.front(opdata) &&
						opdata &&
						fistdata->buffer.Length() + opdata->buffer.Length() < SENDBUFFERCONCATMAXLEN)
					{
						fistdata->buffer.Append(opdata->buffer);
						_queue.dequeue_front(opdata);
						IODATAMANAGER->ReleaseData(opdata);
						count--;
					}
					else
					{
						break;
					}
				}
			}

			out.emplace_back(fistdata);
			_state = submitstate::doing;
		}
	}
}

std::shared_ptr<IOCPCoreProcessImpl::SequentialEventExecutor::ExcuteEvent>
IOCPCoreProcessImpl::SequentialEventExecutor::ExcuteEvent::CreateReadEvent(std::shared_ptr<NetCore_IOCPData> iodata, Buffer& buf)
{
	std::shared_ptr<ExcuteEvent> event = std::make_shared<ExcuteEvent>(EventType::READ_DATA, iodata);
	event->data = std::make_shared<Buffer>();
	event->data->QuoteFromBuf(buf);
	return event;
}

std::shared_ptr<IOCPCoreProcessImpl::SequentialEventExecutor::ExcuteEvent>
IOCPCoreProcessImpl::SequentialEventExecutor::ExcuteEvent::CreateAcceptEvent(std::shared_ptr<NetCore_IOCPData> iodata, int client_fd, sockaddr_in client_addr)
{
	std::shared_ptr<ExcuteEvent> event = std::make_shared<ExcuteEvent>(EventType::ACCEPT_CONNECTION, iodata);
	event->accept_info.client_fd = client_fd;
	event->accept_info.client_addr = client_addr;
	return event;
}

std::shared_ptr<IOCPCoreProcessImpl::SequentialEventExecutor::ExcuteEvent>
IOCPCoreProcessImpl::SequentialEventExecutor::ExcuteEvent::CreateRDHUP(std::shared_ptr<NetCore_IOCPData> iodata)
{
	std::shared_ptr<ExcuteEvent> event = std::make_shared<ExcuteEvent>(EventType::READ_HUP, iodata);
	return event;
}

IOCPCoreProcessImpl::SequentialEventExecutor::SequentialEventExecutor(
	IOCPCoreProcessImpl* core,
	std::shared_ptr<NetCore_IOCPData>& data)
	: _core(core), _weakdata(data), _state(excutestate::idle), _active(true)
{
}

IOCPCoreProcessImpl::SequentialEventExecutor::~SequentialEventExecutor()
{
	Release();
}

void IOCPCoreProcessImpl::SequentialEventExecutor::Release()
{
	_state = excutestate::none;
	std::lock_guard<CriticalSectionLock> lock(_lock);
	_queue.clear();
}

void IOCPCoreProcessImpl::SequentialEventExecutor::SubmitExcuteEvent(std::shared_ptr<ExcuteEvent> event)
{
	if (!_core)
		return;
	if (_state == excutestate::none)
		return;

	std::lock_guard<CriticalSectionLock> lock(_lock);
	if (_state == excutestate::none || !_weakdata.lock())
		return;

	_queue.enqueue(event);

	if (_state == excutestate::idle)
	{
		_core->_ExcuteCV.NotifyOne();
	}
}

void IOCPCoreProcessImpl::SequentialEventExecutor::NotifyDone()
{
	if (_state == excutestate::none)
		return;

	std::lock_guard<CriticalSectionLock> lock(_lock);
	if (_state == excutestate::none)
		return;

	_state = excutestate::idle;
	if (!_queue.empty())
		_core->_ExcuteCV.NotifyOne();
}

void IOCPCoreProcessImpl::SequentialEventExecutor::GetPostExcuteEvent(std::vector<std::shared_ptr<ExcuteEvent>>& out)
{
	if (_state != excutestate::idle)
		return;

	std::lock_guard<CriticalSectionLock> lock(_lock);
	if (_state != excutestate::idle)
		return;

	if (_queue.empty())
		return;

	std::shared_ptr<ExcuteEvent> firstevent;
	if (_queue.dequeue(firstevent) && firstevent)
	{
		if (firstevent->type == ExcuteEvent::EventType::READ_DATA) // 尝试合并后续的读事件
		{
			int count = 9;
			while (!_queue.empty() && count > 0)
			{
				std::shared_ptr<ExcuteEvent> event;
				if (_queue.front(event) &&
					event &&
					event->type == firstevent->type &&
					event->data &&
					firstevent->data->Length() + event->data->Length() < RECVBUFFERCONCATMAXLEN)
				{
					firstevent->data->Append(*(event->data));
					_queue.dequeue(event);
					count--;
				}
				else
				{
					break;
				}
			}
		}

		out.emplace_back(firstevent);
		_state = excutestate::doing;
	}
}

bool IOCPCoreProcessImpl::SequentialEventExecutor::Active()
{
	return _active;
}

void IOCPCoreProcessImpl::SequentialEventExecutor::SetActive(bool value)
{
	_active = value;
}

IOCPCoreProcessImpl::DynamicBufferState::DynamicBufferState()
{
	lastbuffersize = RECVBUFFERDEFLEN;
}

void IOCPCoreProcessImpl::DynamicBufferState::Update(uint32_t newbufferrecvlen)
{
	if (newbufferrecvlen >= lastbuffersize)
	{
		if (lastbuffersize < RECVBUFFERMAXLEN)
			lastbuffersize = min((uint32_t)RECVBUFFERMAXLEN, (uint32_t)((float)lastbuffersize * 1.5f));
	}
	else
	{
		if (lastbuffersize > RECVBUFFERMINLEN)
			lastbuffersize = max((uint32_t)RECVBUFFERMINLEN, max(newbufferrecvlen, (uint32_t)((float)lastbuffersize * 0.67f)));
	}
}

uint32_t IOCPCoreProcessImpl::DynamicBufferState::GetDynamicSize()
{
	return lastbuffersize;
}

IOCPCoreProcessImpl::IOCPCoreProcessImpl()
	: _shouldshutdown(false), _isinitsuccess(false), _isrunning(false), _ExcuteEventProcessPool(4), _iocp(NULL)
{
	_isinitsuccess = CreateIOCP(_iocp);
}

IOCPCoreProcessImpl::~IOCPCoreProcessImpl()
{
	Stop();
}

int IOCPCoreProcessImpl::Run()
{
	try
	{
		_isrunning = true;
		_shouldshutdown = false;
		if (!_isinitsuccess)
		{
			_isinitsuccess = CreateIOCP(_iocp);
		}

		_ExcuteEventProcessPool.start();
		std::thread IOEventLoop(&IOCPCoreProcessImpl::LoopSubmitIOEvent, this);
		std::thread ExcuteEventLoop(&IOCPCoreProcessImpl::LoopSubmitExcuteEvent, this);
		std::thread EventLoop(&IOCPCoreProcessImpl::Loop, this);
		EventLoop.join();
		IOEventLoop.join();
		ExcuteEventLoop.join();
		_ExcuteEventProcessPool.stop();

		if (_iocp && _iocp != INVALID_HANDLE_VALUE)
		{
			CloseHandle(_iocp);
			_iocp = 0;
			_isinitsuccess = false;
		}

		_isrunning = false;

		// ThreadEnd();
		return 1;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << '\n';
		return -1;
	}
}

bool IOCPCoreProcessImpl::Running()
{
	return _isrunning;
}

void IOCPCoreProcessImpl::Stop()
{
	IOCPOPData* opdata = IODATAMANAGER->AllocateData(IOCP_OPType::OP_SHUTDOWN, shutdown_eventfd, nullptr, 0);
	opdata->fd = shutdown_eventfd;
	if (_isinitsuccess)
	{
		PostQueuedCompletionStatus(
			_iocp,
			0,                  // dwNumberOfBytesTransferred
			NULL,				// dwCompletionKey
			&opdata->overlapped // lpOverlapped
		);
	}
}

bool IOCPCoreProcessImpl::AddNetFd(std::shared_ptr<BaseTransportConnection> Con)
{
	if (!Con || Con->GetSocket() <= 0)
		return false;

	std::shared_ptr<NetCore_IOCPData> iodata = std::make_shared<NetCore_IOCPData>();
	std::weak_ptr<BaseTransportConnection> weak(Con);
	iodata->fd = Con->GetSocket();
	iodata->weakCon = weak;
	iodata->recver = std::make_shared<SequentialEventExecutor>(this, iodata);
	iodata->state = std::make_shared<DynamicBufferState>();

	if (!AssociateSocketWithIOCP(iodata->fd, (ULONG_PTR)iodata->fd, Con.get()))
		return false;

	if (Con->GetNetType() == NetType::Client)
	{
		iodata->senders.emplace_back(std::make_shared<SequentialIOSubmitter>(this, iodata, IOCP_OPType::OP_WRITE));
		iodata->senders.emplace_back(std::make_shared<SequentialIOSubmitter>(this, iodata, IOCP_OPType::OP_READ));
		iodata->senders.emplace_back(std::make_shared<SequentialIOSubmitter>(this, iodata, IOCP_OPType::OP_WILLWRITE));
		_IOCPData.Insert(Con.get(), iodata);

		IOCPOPData* opdata = IOCPOPData::CreateReadOP(Con, iodata->state->GetDynamicSize());
		iodata->SubmitIOEvent(opdata);
	}
	else if (Con->GetNetType() == NetType::Listener)
	{
		iodata->senders.emplace_back(std::make_shared<SequentialIOSubmitter>(this, iodata, IOCP_OPType::OP_ACCEPT));
		_IOCPData.Insert(Con.get(), iodata);

		IOCPOPData* opdata = IOCPOPData::CreateAcceptOP(Con);
		iodata->SubmitIOEvent(opdata);
	}

	return true;
}

bool IOCPCoreProcessImpl::DelNetFd(BaseTransportConnection* Con)
{
	_IOCPData.Erase(Con);
	_associateSocketMap.EnsureCall(
		[Con](std::map<BaseSocket, BaseTransportConnection*> map)->void
		{
			int count = 0;
			for (auto it = map.begin(); it != map.end();)
			{
				if (it->second == Con)
				{
					it = map.erase(it);
					return;
				}
				it++;
			}
		}
	);
	return true;
}

bool IOCPCoreProcessImpl::SendRes(std::shared_ptr<BaseTransportConnection> BaseCon)
{
	if (!BaseCon || !(BaseCon->GetNetType() == NetType::Client))
		return false;

	std::shared_ptr<TCPTransportConnection> Con = BaseCon->GetShared<TCPTransportConnection>();
	if (!Con)
		return false;

	LockGuard lock(Con->GetSendMtx(), true);
	if (!lock.isownlock())
		return true; // 写锁正在被其他线程占用

	int fd = Con->GetSocket();
	SafeQueue<Buffer*>& SendDatas = Con->GetSendData();

	std::shared_ptr<NetCore_IOCPData> iodata;
	if (!_IOCPData.Find(Con.get(), iodata))
	{
		iodata = std::make_shared<NetCore_IOCPData>();
		auto weak = std::weak_ptr<BaseTransportConnection>(BaseCon);
		iodata->fd = Con->GetSocket();
		iodata->weakCon = weak;
		iodata->senders.emplace_back(std::make_shared<SequentialIOSubmitter>(this, iodata, IOCP_OPType::OP_WRITE));
		iodata->senders.emplace_back(std::make_shared<SequentialIOSubmitter>(this, iodata, IOCP_OPType::OP_READ));
		iodata->senders.emplace_back(std::make_shared<SequentialIOSubmitter>(this, iodata, IOCP_OPType::OP_WILLWRITE));
		iodata->recver = std::make_shared<SequentialEventExecutor>(this, iodata);
		iodata->state = std::make_shared<DynamicBufferState>();
		if (!_IOCPData.Insert(Con.get(), iodata))
		{
			if (!_IOCPData.Find(Con.get(), iodata))
				return false;
		}
	}

	int count = 0;
	while (count < 10 && !SendDatas.empty())
	{

		Buffer* buffer = nullptr;
		if (!SendDatas.dequeue(buffer))
			break;

		if (buffer)
		{
			if (!buffer->Data() || buffer->Length() <= 0)
			{
				// 无效Buffer不发送
				SAFE_DELETE(buffer);
			}
			else
			{

				IOCPOPData* opdata = IOCPOPData::CreateWriteOP(Con, *buffer);
				bool submit = iodata->SubmitIOEvent(opdata);
				SAFE_DELETE(buffer);
				if (!submit)
					break;
			}
		}
		count++;
	}

	if (!SendDatas.empty()) // 仍有数据未发送,关注其可写事件,等待下次可写事件
	{
		IOCPOPData* opdata = IOCPOPData::CreateWillWriteOP(Con);
		if (!iodata->SubmitIOEvent(opdata))
			return false;
	}

	return true;
}

void IOCPCoreProcessImpl::LoopSubmitIOEvent()
{

	if (!_isrunning || _shouldshutdown)
		return;

	do
	{
		LockGuard guard(_IOEventLock);
		_IOEventCV.WaitFor(guard, std::chrono::milliseconds(50));

		if (_shouldshutdown || !_isrunning)
			break;

		std::vector<IOCPOPData*> opdatas;
		auto GetEvents = [&opdatas](std::map<BaseTransportConnection*, std::shared_ptr<IOCPCoreProcessImpl::NetCore_IOCPData>>& map) -> void
			{
				for (const auto& [Con, iodata] : map)
				{
					for (size_t i = 0; i < iodata->senders.size(); ++i)
					{
						iodata->senders[i]->GetPostIOEvent(opdatas);
					}
				}
			};
		_IOCPData.EnsureCall(GetEvents);
		if (opdatas.size() > 0)
			DoPostIOEvents(opdatas);
	} while ((_isrunning && !_shouldshutdown));
}

void IOCPCoreProcessImpl::LoopSubmitExcuteEvent()
{
	if (!_isrunning || _shouldshutdown)
		return;

	do
	{
		LockGuard guard(_ExcuteLock);
		_ExcuteCV.WaitFor(guard, std::chrono::milliseconds(50));

		if (_shouldshutdown || !_isrunning)
			break;

		std::vector<std::shared_ptr<SequentialEventExecutor::ExcuteEvent>> events;
		auto GetEvents = [&events](std::map<BaseTransportConnection*, std::shared_ptr<IOCPCoreProcessImpl::NetCore_IOCPData>>& map) -> void
			{
				for (auto it = map.begin(); it != map.end(); it++)
				{
					auto& iodata = it->second;
					iodata->recver->GetPostExcuteEvent(events);
				}
			};
		_IOCPData.EnsureCall(GetEvents);
		if (events.size() > 0)
			DoPostExcuteEvents(events);
	} while (_isrunning && !_shouldshutdown);
}

void IOCPCoreProcessImpl::Loop()
{
	std::cout << "IOCPProcess , EventLoop\n";

	while (_isrunning && !_shouldshutdown)
	{
		std::vector<IOCPOPData*> opdatas;
		if (!GetDoneIOEvents(opdatas))
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
			break;
		}

		std::vector<IOCPOPData*> postOps;
		uint32_t otherprocesscount = 0;
		for (int i = 0; i < opdatas.size(); i++)
		{
			auto opdata = opdatas[i];
			if (opdata->OP_Type == IOCP_OPType::OP_SHUTDOWN)
			{
				_shouldshutdown = true;
				std::cout << "IOCPProcess , EventLoop closing......\n";
				IODATAMANAGER->ReleaseData(opdata);
				continue;
			}
			EventProcess(opdata, postOps);
			IODATAMANAGER->ReleaseData(opdata);
		}
		opdatas.clear();

		ProcessPendingDeletions();

		if (_shouldshutdown)
			break;

		if (postOps.size() > 0)
		{
			DoPostIOEvents(postOps);
			postOps.clear();
		}
	}
}

bool IOCPCoreProcessImpl::GetDoneIOEvents(std::vector<IOCPOPData*>& opdatas)
{
	static DWORD timeOutms = 200;
	DWORD completedCount = 0;
	constexpr int batchsize = 100;

	while (true)
	{
		std::vector<OVERLAPPED_ENTRY> entries(batchsize);
		ULONG recvsize = 0;

		// 批量获取完成事件
		BOOL success = GetQueuedCompletionStatusEx(
			_iocp,
			entries.data(),
			batchsize,
			&recvsize,
			timeOutms,
			FALSE
		);

		if (!success || recvsize == 0) {
			break;
		}

		// 处理批量完成事件
		for (ULONG i = 0; i < recvsize; i++) {
			OVERLAPPED_ENTRY& entry = entries[i];

			if (entry.lpOverlapped != NULL)
			{
				IOCPOPData* opdata = CONTAINING_RECORD(entry.lpOverlapped, IOCPOPData, overlapped);

				if (opdata)
				{
					opdata->res = entry.dwNumberOfBytesTransferred;
					// opdata->completionKey = entry.lpCompletionKey;

					if (opdata->OP_Type == IOCP_OPType::OP_ACCEPT)
					{
						setsockopt(opdata->client_fd, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
							(char*)&opdata->fd, sizeof(opdata->fd));

						sockaddr_in* localAddr = nullptr;
						sockaddr_in* remoteAddr = nullptr;
						int localAddrLen = 0;
						int remoteAddrLen = 0;

						GetAcceptExSockaddrs(
							opdata->buffer.Data(),
							0,
							addrBufSize,
							addrBufSize,
							(LPSOCKADDR*)&localAddr,
							&localAddrLen,
							(LPSOCKADDR*)&remoteAddr,
							&remoteAddrLen
						);

						if (!remoteAddr || remoteAddrLen < sizeof(sockaddr_in)) //error
						{
							memset(&opdata->client_addr, 0, sizeof(sockaddr_in));
							if (opdata->client_fd != Invaild_Socket)
							{
								CloseSocket(opdata->client_fd);
								opdata->client_fd = Invaild_Socket;
							}
							continue;
						}
						else {
							opdata->client_addr = *remoteAddr;
						}
					}

					opdatas.push_back(opdata);
				}
			}

			completedCount++;
		}

		if (recvsize < batchsize) {
			break;
		}
	}

	return true;
}

int IOCPCoreProcessImpl::EventProcess(IOCPOPData* opdata, std::vector<IOCPOPData*>& postOps)
{
	if (!opdata)
		return -1;

	std::shared_ptr<BaseTransportConnection> Con = opdata->weakCon.lock();
	if (!Con) // 检查是否失效
	{
		DelNetFd(opdata->raw_ptr);
		return -1;
	}

	int res = opdata->res;
	int fd = opdata->fd;
	IOCP_OPType OP_Type = opdata->OP_Type;

	if (fd <= 0 || !Con->ValidSocket())
	{
		return -1;
	}

	DWORD dwFlags = 0;
	DWORD dwBytesTransferred = 0;
	BOOL bResult = WSAGetOverlappedResult(
		opdata->fd,
		&opdata->overlapped,
		&dwBytesTransferred,
		FALSE,
		&dwFlags
	);

	if (!bResult) {
		DWORD dwError = WSAGetLastError();
		// 处理错误
		if (dwError == WSAENOBUFS || dwError == WSA_NOT_ENOUGH_MEMORY)
		{
			std::shared_ptr<NetCore_IOCPData> iodata;
			if (_IOCPData.Find(Con.get(), iodata))
			{
				if (OP_Type == IOCP_OPType::OP_WRITE)
				{
					iodata->NotifyIOEventRetry(opdata);
				}
				else if (OP_Type == IOCP_OPType::OP_WILLWRITE)
				{
					iodata->NotifyIOEventRetry(opdata);
				}
				else if (OP_Type == IOCP_OPType::OP_READ)
				{
					auto readop = IOCPOPData::CreateReadOP(Con, opdata->buffer.Length());
					postOps.emplace_back(readop);
				}
				else if (OP_Type == IOCP_OPType::OP_ACCEPT)
				{
					auto acceptop = IOCPOPData::CreateAcceptOP(Con);
					postOps.emplace_back(acceptop);
				}
				return 0;
			}
			else
			{
				return -1;
			}
		}
		else
		{
			std::shared_ptr<NetCore_IOCPData> iodata;
			if (_IOCPData.Find(Con.get(), iodata) && iodata && iodata->recver)
			{
				if (iodata->recver->Active())
				{
					iodata->recver->SetActive(false);
					auto rdhupevent = SequentialEventExecutor::ExcuteEvent::CreateRDHUP(iodata);
					iodata->recver->SubmitExcuteEvent(rdhupevent);
				}
			}
			else
			{
				DelNetFd(Con.get());
			}
			return -1;
		}

		return -1;
	}

	switch (OP_Type)
	{
	case IOCP_OPType::OP_WRITE:
	{
		int bufferwrite = res;
		opdata->buffer.Seek(opdata->buffer.Position() + bufferwrite);
		std::shared_ptr<NetCore_IOCPData> iodata;
		if (_IOCPData.Find(Con.get(), iodata) && iodata)
		{
			iodata->NotifyIOEventDone(opdata);
		}
		break;
	}
	case IOCP_OPType::OP_WILLWRITE:
	{
		if (Con->GetSocket() == fd && Con->GetNetType() == NetType::Client)
		{
			std::shared_ptr<TCPTransportConnection> tcpCon(
				Con, static_cast<TCPTransportConnection*>(Con.get()));
			SendRes(tcpCon);
		}
		std::shared_ptr<NetCore_IOCPData> iodata;
		if (_IOCPData.Find(Con.get(), iodata) && iodata)
		{
			iodata->NotifyIOEventDone(opdata);
		}
		break;
	}
	case IOCP_OPType::OP_READ:
	{
		std::shared_ptr<NetCore_IOCPData> iodata;
		if (_IOCPData.Find(Con.get(), iodata) && iodata)
		{
			int bufferlen = res;
			if (bufferlen > 0)
			{
				iodata->state->Update(bufferlen);
				Buffer& buf = opdata->buffer;
				Buffer buffer(buf.Byte(), bufferlen);
				if (iodata->recver->Active())
				{
					auto readevent = SequentialEventExecutor::ExcuteEvent::CreateReadEvent(iodata, buffer);
					iodata->recver->SubmitExcuteEvent(readevent);
				}
			}

			iodata->NotifyIOEventDone(opdata);
			auto readop = IOCPOPData::CreateReadOP(Con, iodata->state->GetDynamicSize());
			postOps.emplace_back(readop);
		}
		break;
	}
	case IOCP_OPType::OP_ACCEPT:
	{
		BaseSocket client_fd = opdata->client_fd;
		std::shared_ptr<NetCore_IOCPData> iodata;
		if (_IOCPData.Find(Con.get(), iodata) && iodata)
		{
			if (client_fd > 0 && Con->GetNetType() == NetType::Listener)
			{
				sockaddr_in client_addr = opdata->client_addr;
				if (iodata->recver->Active())
				{
					auto acceptevent = SequentialEventExecutor::ExcuteEvent::CreateAcceptEvent(iodata, client_fd, client_addr);
					iodata->recver->SubmitExcuteEvent(acceptevent);
				}
			}
			iodata->NotifyIOEventDone(opdata);
			auto acceptop = IOCPOPData::CreateAcceptOP(Con);
			postOps.emplace_back(acceptop);
		}
		break;
	}

	default:
		break;
	}

	return 1;
}

void IOCPCoreProcessImpl::DoPostIOEvents(std::vector<IOCPOPData*> opdatas)
{
	std::lock_guard<CriticalSectionLock> lock(_doPostIOEventLock);

	for (auto opdata : opdatas)
	{
		switch (opdata->OP_Type)
		{
		case IOCP_OPType::OP_WRITE:
		{
			SubmitWriteEvent(opdata);
		}
		break;
		case IOCP_OPType::OP_READ:
		{
			SubmitReadEvent(opdata);
		}
		break;
		case IOCP_OPType::OP_ACCEPT:
		{
			SubmitAcceptEvent(opdata);
		}
		break;
		case IOCP_OPType::OP_WILLWRITE:
		{
			SubmitWillWriteEvent(opdata);
		}
		break;

		default:
			IODATAMANAGER->ReleaseData(opdata);
			break;
		}
	}
}

void IOCPCoreProcessImpl::DoPostExcuteEvents(std::vector<std::shared_ptr<SequentialEventExecutor::ExcuteEvent>>& events)
{
	auto task =
		[this](std::shared_ptr<SequentialEventExecutor::ExcuteEvent> event) -> void
		{
			if (!event)
				return;

			auto iodata = event->weakdata.lock();
			if (!iodata)
				return;

			try
			{
				auto connection = iodata->weakCon.lock();
				if (event->type == SequentialEventExecutor::ExcuteEvent::EventType::READ_HUP)
					this->DelNetFd(connection.get());
				if (connection)
				{
					if (event->type == SequentialEventExecutor::ExcuteEvent::EventType::READ_DATA)
					{
						Buffer& buf = *(event->data);
						connection->READ(connection->GetSocket(),
							buf);
					}
					else if (event->type == SequentialEventExecutor::ExcuteEvent::EventType::ACCEPT_CONNECTION)
						connection->ACCEPT(connection->GetSocket(),
							event->accept_info.client_fd, event->accept_info.client_addr);
					else if (event->type == SequentialEventExecutor::ExcuteEvent::EventType::READ_HUP)
					{
						connection->RDHUP();
					}
				}
			}
			catch (const std::exception& e)
			{
				std::cerr << "NetCoreCallback Error!" << e.what() << '\n';
			}

			auto recver = iodata->recver;
			if (recver)
				recver->NotifyDone();
		};
	for (auto event : events)
	{
		_ExcuteEventProcessPool.submit(task, event);
	}
}

bool IOCPCoreProcessImpl::SubmitAcceptEvent(IOCPOPData* opdata)
{
	if (auto Con = opdata->weakCon.lock())
	{
		opdata->client_fd = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
		if (opdata->client_fd == INVALID_SOCKET)
			return false;

		if (!AssociateSocketWithIOCP(opdata->client_fd, (ULONG_PTR)opdata->client_fd))
		{
			CloseSocket(opdata->client_fd);
			opdata->client_fd = INVALID_SOCKET;
			return false;
		}

		opdata->buffer.ReSize(2 * addrBufSize);

		BOOL result = AcceptEx(
			opdata->fd,
			opdata->client_fd,
			opdata->buffer.Data(),
			0,
			addrBufSize,
			addrBufSize,
			&opdata->res,
			&opdata->overlapped
		);

		if (!result) {
			int error = WSAGetLastError();
			if (error != ERROR_IO_PENDING) {
				CloseSocket(opdata->client_fd);
				opdata->client_fd = INVALID_SOCKET;
				return false;
			}
		}
		return true;
	}
	return false;
}

bool IOCPCoreProcessImpl::SubmitWriteEvent(IOCPOPData* opdata)
{
	if (auto Con = opdata->weakCon.lock())
	{
		if (opdata->fd == Invaild_Socket)
			return false;

		opdata->wsaBuf.buf = opdata->buffer.Byte() + opdata->buffer.Position();
		opdata->wsaBuf.len = opdata->buffer.Remain();

		int result = WSASend(
			opdata->fd,
			&opdata->wsaBuf,
			1,
			&opdata->res,
			0,
			&opdata->overlapped,
			NULL
		);

		if (result == SOCKET_ERROR) {
			int error = WSAGetLastError();
			if (error != WSA_IO_PENDING) {
				return false;
			}
		}
		return true;
	}
	return false;
}

bool IOCPCoreProcessImpl::SubmitReadEvent(IOCPOPData* opdata)
{
	if (auto Con = opdata->weakCon.lock())
	{
		if (opdata->fd == INVALID_SOCKET)
			return false;

		opdata->wsaBuf.buf = opdata->buffer.Byte();
		opdata->wsaBuf.len = opdata->buffer.Length();

		DWORD flags = 0;

		// 发起异步接收
		int result = WSARecv(
			opdata->fd,
			&opdata->wsaBuf,
			1,
			&opdata->res,
			&flags,
			&opdata->overlapped,
			NULL
		);

		if (result == SOCKET_ERROR) {
			int error = WSAGetLastError();
			if (error != WSA_IO_PENDING) {
				return false;
			}
		}
		return true;
	}

	return false;
}

bool IOCPCoreProcessImpl::SubmitWillWriteEvent(IOCPOPData* opdata)
{
	if (auto Con = opdata->weakCon.lock())
	{
		if (opdata->fd == Invaild_Socket)
			return false;

		assert(opdata->wsaBuf.len == 0);

		int result = WSASend(
			opdata->fd,
			&opdata->wsaBuf,
			1,
			&opdata->res,
			0,
			&opdata->overlapped,
			NULL
		);

		if (result == SOCKET_ERROR) {
			int error = WSAGetLastError();
			if (error != WSA_IO_PENDING) {
				return false;
			}
		}
		return true;
	}
	return false;
}

bool IOCPCoreProcessImpl::AssociateSocketWithIOCP(SOCKET socket, ULONG_PTR completionKey, BaseTransportConnection* Con)
{
	HANDLE iocp = _iocp;
	if (iocp == NULL)
		return false;

	HANDLE socketHandle = (HANDLE)socket;
	HANDLE hResult = CreateIoCompletionPort(
		socketHandle,
		iocp,
		completionKey,
		0
	);

	bool success = (hResult == iocp);
	if (!success) {
		DWORD dwError = GetLastError();
		if (dwError == ERROR_INVALID_PARAMETER)
		{
			BaseTransportConnection* temp;
			LockGuard guard = _associateSocketMap.MakeLockGuard();
			if (_associateSocketMap.Find(socket, temp))
			{
				if (temp == nullptr)
				{
					if (Con != nullptr)
					{
						_associateSocketMap[socket] = Con;
						return true;
					}
				}
				else
				{
					return temp == Con;
				}
			}
		}
		else
		{
			return false;
		}
		return false;
	}
	else
	{
		_associateSocketMap[socket] = Con;
	}

	return success;
}

bool isNetObjectAndOnCallBack(DeleteLaterImpl* ptr)
{
	if (BaseNetWorkSession* Session = dynamic_cast<BaseNetWorkSession*>(ptr))
	{
		if (auto EndPoint = Session->GetBaseClient())
		{
			if (auto BaseCon = EndPoint->GetBaseCon())
			{
				bool result = BaseCon->isOnCallback();
				return result;
			}
		}
	}

	if (TCPEndPoint* EndPoint = dynamic_cast<TCPEndPoint*>(ptr))
	{
		if (auto BaseCon = EndPoint->GetBaseCon())
		{
			return BaseCon->isOnCallback();
		}
	}

	if (BaseTransportConnection* BaseCon = dynamic_cast<BaseTransportConnection*>(ptr))
	{
		return BaseCon->isOnCallback();
	}

	return false;
}

void IOCPCoreProcessImpl::ProcessPendingDeletions()
{
	std::vector<DeleteLaterImpl*> deletions;
	_pendingDeletions.EnsureCall(
		[&deletions](std::vector<DeleteLaterImpl*>& array) -> void
		{
			deletions.swap(array); // 取出待删除任务
		}

	);
	for (auto ptr : deletions)
	{
		if (ptr)
		{
			// 由于线程池是异步回调,所以这里删除的时候需要检查是否在执行回调
			// 回调中的连接，退避处理
			if (isNetObjectAndOnCallBack(ptr))
			{
				_pendingDeletions.emplace(ptr);
				continue;
			}
		}
		SAFE_DELETE(ptr);
	}
}

void IOCPCoreProcessImpl::AddPendingDeletion(DeleteLaterImpl* ptr)
{
	_pendingDeletions.emplace(ptr);
}

IOCPCoreProcess* IOCPCoreProcess::Instance()
{
	static IOCPCoreProcess* m_instance = new IOCPCoreProcess();
	return m_instance;
}

IOCPCoreProcess::IOCPCoreProcess()
{
	pImpl = std::make_unique<IOCPCoreProcessImpl>();
}

int IOCPCoreProcess::Run() { return pImpl->Run(); }
void IOCPCoreProcess::Stop() { pImpl->Stop(); }
bool IOCPCoreProcess::Running() { return pImpl->Running(); }
bool IOCPCoreProcess::AddNetFd(std::shared_ptr<BaseTransportConnection> Con) { return pImpl->AddNetFd(Con); }
bool IOCPCoreProcess::DelNetFd(BaseTransportConnection* Con) { return pImpl->DelNetFd(Con); }
bool IOCPCoreProcess::SendRes(std::shared_ptr<BaseTransportConnection> BaseCon) { return pImpl->SendRes(BaseCon); }
void IOCPCoreProcess::AddPendingDeletion(DeleteLaterImpl* ptr) { return pImpl->AddPendingDeletion(ptr); }
