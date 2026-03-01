#include "Coroutine.h"
#include "string.h"
#include "CoroutineScheduler_Win.h"
#include "Timer.h"
#include "SpinLock.h"

#define SAFE_DELETE(x) \
    if (x)             \
    {                  \
        delete x;      \
        x = nullptr;   \
    }

AsyncConnector::AsyncConnector() {}
AsyncConnector::~AsyncConnector()
{
	Cleanup();
}

void AsyncConnector::Connect(SOCKET s, const std::string& ip, const int port, void* userdata)
{
	WSAEVENT hEvent = WSACreateEvent();
	WSAEventSelect(s, hEvent, FD_CONNECT);

	sockaddr_in remoteaddr;
	ZeroMemory(&remoteaddr, sizeof(remoteaddr));
	remoteaddr.sin_family = AF_INET;
	remoteaddr.sin_port = htons(port);
	inet_pton(AF_INET, ip.c_str(), &(remoteaddr.sin_addr.s_addr));
	contexts.emplace(SocketContext{ s, hEvent, ip, port,userdata });

	::connect(s, (sockaddr*)&remoteaddr, sizeof(remoteaddr));
}

bool AsyncConnector::GetDoneEvents(std::vector<ConnectEvent>& events)
{
	bool result = false;
	ConnectEvent event;
	while (GetEvent(event)) {
		events.emplace_back(std::move(event));
		result = true;
	}
	return result;
}

bool AsyncConnector::GetEvent(ConnectEvent& event)
{
	if (contexts.empty())
		return false;

	std::vector<WSAEVENT> events;

	contexts.EnsureCall(
		[&events](std::vector<AsyncConnector::SocketContext>& array)->void {
			for (auto& ctx : array)
				events.push_back(ctx.hEvent);
		}
	);

	// 等待事件
	DWORD dwIndex = WSAWaitForMultipleEvents(
		(DWORD)events.size(),
		events.data(),
		FALSE,  // 任一事件触发
		0,		// 不等待立即返回
		FALSE
	);

	if (dwIndex == WSA_WAIT_FAILED) {
		//printf("AsyncConnector WaitEvents 失败: %d\n", WSAGetLastError());
		return false;
	}

	if (dwIndex == WSA_WAIT_TIMEOUT) {
		return false;  // 没有事件就绪
	}

	if (dwIndex >= WSA_WAIT_EVENT_0 &&
		dwIndex < WSA_WAIT_EVENT_0 + events.size()) {

		// 计算触发的事件索引
		int eventIdx = dwIndex - WSA_WAIT_EVENT_0;

		// 通过索引找到对应的 socket 上下文
		SocketContext& ctx = contexts[eventIdx];

		// 枚举该 socket 上的所有事件
		WSANETWORKEVENTS netEvents;
		WSAEnumNetworkEvents(ctx.s, ctx.hEvent, &netEvents);

		//printf("Socket %lld (%s:%d) 触发事件\n",
		//	ctx.s, ctx.address.c_str(), ctx.port);

		if (netEvents.lNetworkEvents & FD_CONNECT)
		{
			int error = netEvents.iErrorCode[FD_CONNECT_BIT];
			//printf("  - FD_CONNECT, 错误码: %d\n", error);
			event.error = error;
		}
		event.s = ctx.s;
		event.userdata = ctx.userdata;
		auto guard = contexts.MakeLockGuard();
		WSAEventSelect(ctx.s, NULL, 0);
		contexts.deleteIndexElement(eventIdx);
		return true;
	}
	return false;
}

void AsyncConnector::Cleanup()
{
	contexts.EnsureCall(
		[](std::vector<AsyncConnector::SocketContext>& array)->void {
			for (auto& ctx : array)
				WSAEventSelect(ctx.s, NULL, 0);
			array.clear();
		}
	);
}

#define ENTRIES 3000

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

class CoroutineScheduler;

enum class Coro_IOCP_OPType
{
	OP_TimeOut = 1,   // 定时器
	OP_Coroutine = 2, // 协程执行
	OP_Connect = 3,   // socket connect
	OP_SHUTDOWN
};
struct Coro_IOCPOPData
{
	OVERLAPPED overlapped;

	Coro_IOCP_OPType OP_Type;
	DWORD res = 0;

	std::shared_ptr<TaskHandle> task_handle;
	std::shared_ptr<CoConnection::Handle> connection_handle;
	std::shared_ptr<CoTimer::Handle> timer_handle;

	Coro_IOCPOPData(Coro_IOCP_OPType OP_Type)
		: OP_Type(OP_Type) {
		memset(&overlapped, 0, sizeof(OVERLAPPED));
	}
	Coro_IOCPOPData(std::shared_ptr<TaskHandle> handle)
		: OP_Type(Coro_IOCP_OPType::OP_Coroutine), task_handle(handle) {
		memset(&overlapped, 0, sizeof(OVERLAPPED));
	}
	Coro_IOCPOPData(std::shared_ptr<CoConnection::Handle> handle)
		: OP_Type(Coro_IOCP_OPType::OP_Connect), connection_handle(handle) {
		memset(&overlapped, 0, sizeof(OVERLAPPED));
	}
	Coro_IOCPOPData(std::shared_ptr<CoTimer::Handle> handle)
		: OP_Type(Coro_IOCP_OPType::OP_TimeOut), timer_handle(handle) {
		memset(&overlapped, 0, sizeof(OVERLAPPED));
	}
};

CoroutineScheduler::CoroutineScheduler()
	: _shouldshutdown(false), _isinitsuccess(false), _isrunning(false), _ExcuteEventProcessPool(4)
{
	_asyncConnector = std::make_unique<AsyncConnector>();
	_isinitsuccess = CreateIOCP(_iocp);

	if (!Running() && _isinitsuccess)
	{
		std::thread CoreThread(&CoroutineScheduler::Run, this);
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		CoreThread.detach();
	}
}

CoroutineScheduler::~CoroutineScheduler()
{
	Stop();
}

int CoroutineScheduler::Run()
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
		std::thread CoTaskReleaseLoop(&CoroutineScheduler::LoopCoTaskRelease, this);
		std::thread IOEventLoop(&CoroutineScheduler::LoopSubmitIOEvent, this);
		std::thread EventLoop(&CoroutineScheduler::Loop, this);
		EventLoop.join();
		IOEventLoop.join();
		CoTaskReleaseLoop.join();
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

bool CoroutineScheduler::Running()
{
	return _isrunning;
}

void CoroutineScheduler::Stop()
{
	if (_isinitsuccess)
	{
		Coro_IOCPOPData* opdata = new Coro_IOCPOPData(Coro_IOCP_OPType::OP_SHUTDOWN);
		PostQueuedCompletionStatus(
			_iocp,
			0,                  // dwNumberOfBytesTransferred
			NULL,				// dwCompletionKey
			&opdata->overlapped // lpOverlapped
		);
	}
}

void CoroutineScheduler::LoopCoTaskRelease()
{
	do
	{

		std::unique_lock<std::mutex> lock(_CoTaskReleaseLock);
		_CoTaskReleaseCV.wait_for(lock, std::chrono::milliseconds(50));

		if (_shouldshutdown || !_isrunning)
			break;

		constexpr size_t BATCH_SIZE = 100;
		while (!_pendingReleaseTasks.empty())
		{
			std::shared_ptr<std::coroutine_handle<>> handle;
			for (size_t i = 0; i < BATCH_SIZE && _pendingReleaseTasks.dequeue(handle); ++i)
				handle.reset();
		}

	} while (_isrunning && !_shouldshutdown);
}

void CoroutineScheduler::LoopSubmitIOEvent()
{
	if (!_isrunning || _shouldshutdown)
		return;

	do
	{

		std::unique_lock<std::mutex> lock(_IOEventLock);
		_IOEventCV.wait_for(lock, std::chrono::milliseconds(50));

		if (_shouldshutdown || !_isrunning)
			break;

		std::vector<Coro_IOCPOPData*> opdatas;
		constexpr size_t BATCH_SIZE = 100;
		while (!_optaskqueue.empty())
		{
			Coro_IOCPOPData* temp = nullptr;
			for (size_t i = 0; i < BATCH_SIZE && _optaskqueue.dequeue(temp); ++i)
			{
				if (temp)
					opdatas.push_back(temp);
			}
			if (!opdatas.empty())
			{
				DoPostIOEvents(opdatas);
				opdatas.clear();
			}
		}

	} while (_isrunning && !_shouldshutdown);
}

void CoroutineScheduler::Loop()
{
	std::cout << "CoroutineScheduler , EventLoop\n";

	while (_isrunning && !_shouldshutdown)
	{
		std::vector<Coro_IOCPOPData*> opdatas;
		if (!GetDoneIOEvents(opdatas))
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
			break;
		}

		uint32_t otherprocesscount = 0;
		for (int i = 0; i < opdatas.size(); i++)
		{
			auto opdata = opdatas[i];
			if (opdata->OP_Type == Coro_IOCP_OPType::OP_SHUTDOWN)
			{
				_shouldshutdown = true;
				std::cout << "IOuringCore , EventLoop closing......\n";
				SAFE_DELETE(opdata);
				continue;
			}
			EventProcess(opdata);
			SAFE_DELETE(opdata);
			if (_shouldshutdown)
				break;
		}
		if (_shouldshutdown)
			break;
		opdatas.clear();
	}
}

bool CoroutineScheduler::GetDoneIOEvents(std::vector<Coro_IOCPOPData*>& opdatas)
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
				Coro_IOCPOPData* opdata = CONTAINING_RECORD(entry.lpOverlapped, Coro_IOCPOPData, overlapped);

				if (opdata)
				{
					if (opdata->OP_Type == Coro_IOCP_OPType::OP_TimeOut)
					{
						opdata->res = entry.dwNumberOfBytesTransferred;
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

	{
		std::vector<AsyncConnector::ConnectEvent> connectEvents;
		_asyncConnector->GetDoneEvents(connectEvents);
		if (!connectEvents.empty())
		{
			for (auto& event : connectEvents)
			{
				if (event.userdata == nullptr)
					continue;

				Coro_IOCPOPData* opdata = (Coro_IOCPOPData*)(event.userdata);
				if (event.error != 0)
				{
					opdata->res = 0;
					if (opdata->connection_handle)
						opdata->connection_handle->socket = 0;
				}
				else
				{
					opdata->res = event.s;
				}
				opdatas.push_back(opdata);
			}
		}
	}

	return true;
}

int CoroutineScheduler::EventProcess(Coro_IOCPOPData* opdata)
{
	if (!opdata)
		return -1;

	if (opdata->OP_Type == Coro_IOCP_OPType::OP_TimeOut)
	{
		auto handle = opdata->timer_handle;

		if (!handle || !handle->active)
			return 1;

		LockGuard lock(handle->corolock);
		if (handle->active)
		{
			std::coroutine_handle<> coro = handle->coroutine;
			if (coro && !coro.done())
			{
				auto task = [coro, handle]() -> void
					{
						if (handle)
						{
							bool expected = false;
							if (handle->corodone.compare_exchange_strong(expected, true))
							{
								if (!coro.done())
								{
									coro.resume();
									//handle->coroutine = nullptr;
								}
							}
						}
					};
				auto expected = CoTimer::WakeType::RUNNING;
				handle->result.compare_exchange_strong(expected, CoTimer::WakeType::TIMEOUT);
				ExcuteCoroutine(task); // 恢复对应的协程
			}
			handle->active = false;
		}
	}
	else if (opdata->OP_Type == Coro_IOCP_OPType::OP_Coroutine)
	{
		auto handle = opdata->task_handle;
		if (!handle)
			return 1;

		if (handle->coroutine && !handle->coroutine.done())
		{
			auto task = [handle]() -> void
				{
					auto coro = handle->coroutine;
					if (!coro.done())
						coro.resume();
				};
			ExcuteCoroutine(task);
		}
	}
	else if (opdata->OP_Type == Coro_IOCP_OPType::OP_Connect)
	{
		auto handle = opdata->connection_handle;
		if (!handle || !handle->active)
			return 1;

		LockGuard lock(handle->corolock);
		if (handle->active)
		{
			std::coroutine_handle<> coro = handle->coroutine;
			if (coro && !coro.done())
			{
				auto task = [coro, handle]() -> void
					{
						if (handle)
						{
							bool expected = false;
							if (handle->corodone.compare_exchange_strong(expected, true))
							{
								if (!coro.done())
								{
									coro.resume();
									//handle->coroutine = nullptr;
								}
							}
						}
					};
				ExcuteCoroutine(task); // 恢复对应的协程
			}
			handle->active = false;
		}
	}
	return 1;
}

void CoroutineScheduler::DoPostIOEvents(std::vector<Coro_IOCPOPData*>& opdatas)
{
	std::lock_guard<CriticalSectionLock> lock(_doPostIOEventLock);

	for (auto opdata : opdatas)
	{
		switch (opdata->OP_Type)
		{
		case Coro_IOCP_OPType::OP_TimeOut:
		{
			SubmitTimeOutEvent(opdata);
		}
		break;
		case Coro_IOCP_OPType::OP_Coroutine:
		{
			SubmitCoroutineEvent(opdata);
		}
		break;
		case Coro_IOCP_OPType::OP_Connect:
		{
			SAFE_DELETE(opdata);
		}
		break;
		default:
			SAFE_DELETE(opdata);
			break;
		}
	}
}

CoroutineScheduler* CoroutineScheduler::Instance()
{
	static CoroutineScheduler* m_instance = new CoroutineScheduler();
	return m_instance;
}

void CoroutineScheduler::DeleteTaskLater(std::shared_ptr<std::coroutine_handle<>> shared)
{
	if (!shared || !(*shared))
		return;

	_pendingReleaseTasks.enqueue(shared);
	_CoTaskReleaseCV.notify_one();
}

std::shared_ptr<CoTimer::Handle> CoroutineScheduler::create_timer(std::chrono::milliseconds interval)
{
	auto handle = std::make_shared<CoTimer::Handle>();

	auto timer = TimerTask::CreateOnce("", interval.count(), [handle, this]()->void {
		Coro_IOCPOPData* opdata = new Coro_IOCPOPData(handle);

		bool success = SubmitTimeOutEvent(opdata);

		if (!success)
			delete opdata;
		});

	if (!timer)
	{
		return std::shared_ptr<CoTimer::Handle>(nullptr);
	}

	handle->task = timer;
	handle->task->Run();
	return handle;
}

// 立即唤醒定时器
void CoroutineScheduler::wake_timer(std::shared_ptr<CoTimer::Handle> handle)
{
	if (!handle || !handle->active)
		return;

	auto expected = CoTimer::WakeType::RUNNING;
	handle->result.compare_exchange_strong(expected, CoTimer::WakeType::MANUAL_WAKE);

	if (!handle->task)
		return;
	handle->task->Wake();
}

std::shared_ptr<TaskHandle> CoroutineScheduler::RegisterTaskCoroutine(std::coroutine_handle<> coroutine)
{
	if (!coroutine || coroutine.done())
		return std::shared_ptr<TaskHandle>();

	auto handle = std::make_shared<TaskHandle>(coroutine);

	Coro_IOCPOPData* opdata = new Coro_IOCPOPData(handle);
	_optaskqueue.enqueue(opdata);
	_IOEventCV.notify_one();

	return handle;
}

std::shared_ptr<CoConnection::Handle> CoroutineScheduler::create_connection(BaseSocket socket, const std::string& IP, int port)
{
	auto handle = std::make_shared<CoConnection::Handle>();
	handle->socket = socket;

	memset(&handle->remoteaddr, 0, sizeof(handle->remoteaddr));
	handle->remoteaddr.sin_family = AF_INET;
	handle->remoteaddr.sin_port = htons(port);
	handle->remoteaddr.sin_addr.s_addr = inet_addr(IP.c_str());

	Coro_IOCPOPData* opdata = new Coro_IOCPOPData(handle);

	_asyncConnector->Connect(socket, IP, port, opdata);

	return handle;
}

bool CoroutineScheduler::SubmitTimeOutEvent(Coro_IOCPOPData* opdata)
{
	return PostQueuedCompletionStatus(
		_iocp,
		0,
		NULL,
		&opdata->overlapped
	) != 0;
}

bool CoroutineScheduler::SubmitCoroutineEvent(Coro_IOCPOPData* opdata)
{
	return PostQueuedCompletionStatus(
		_iocp,
		0,
		NULL,
		&opdata->overlapped
	) != 0;
}

