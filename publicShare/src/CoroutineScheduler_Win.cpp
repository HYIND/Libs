#include "Coroutine.h"
#include "string.h"
#include "CoroutineScheduler_Win.h"
#include "Timer.h"

#define SAFE_DELETE(x) \
    if (x)             \
    {                  \
        delete x;      \
        x = nullptr;   \
    }

#define ENTRIES 3000

static LPFN_CONNECTEX ConnectExPtr = nullptr;

bool ValidateConnectExResult(SOCKET socket) {
	// 步骤1：等待一小段时间让连接完全建立
	Sleep(10);  // 10ms 等待

	// 步骤2：检查多个指标
	bool indicators[3] = { false };

	// 指标1：select 检查
	fd_set writeSet;
	FD_ZERO(&writeSet);
	FD_SET(socket, &writeSet);
	timeval tv = { 0, 10000 };  // 10ms
	indicators[0] = (select(0, nullptr, &writeSet, nullptr, &tv) == 1);

	// 指标2：getpeername 检查
	sockaddr_in addr;
	int len = sizeof(addr);
	indicators[1] = (getpeername(socket, (sockaddr*)&addr, &len) == 0);

	// 指标3：发送测试
	char testByte = 0;
	indicators[2] = (send(socket, &testByte, 0, 0) == 0);

	// 至少两个指标通过才算成功
	int passCount = 0;
	for (bool b : indicators) if (b) passCount++;

	return passCount >= 2;
}

LPFN_CONNECTEX GetConnectExFunction(SOCKET s) {
	LPFN_CONNECTEX ConnectExPtr = nullptr;
	GUID guidConnectEx = WSAID_CONNECTEX;
	DWORD bytes = 0;

	// 通过WSAIoctl获取ConnectEx函数指针
	if (WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER,
		&guidConnectEx, sizeof(guidConnectEx),
		&ConnectExPtr, sizeof(ConnectExPtr),
		&bytes, nullptr, nullptr) == SOCKET_ERROR) {
		return nullptr;
	}
	return ConnectExPtr;
}

bool ConnectPtrNeed(SOCKET socket)
{
	if (::ConnectExPtr)
		return true;

	::ConnectExPtr = GetConnectExFunction(socket);
	return ::ConnectExPtr != nullptr;
}

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
	Coro_IOCPOPData(Coro_IOCP_OPType OP_Type, std::shared_ptr<TaskHandle> handle)
		: OP_Type(OP_Type), task_handle(handle) {
		memset(&overlapped, 0, sizeof(OVERLAPPED));
	}
	Coro_IOCPOPData(Coro_IOCP_OPType OP_Type, std::shared_ptr<CoConnection::Handle> handle)
		: OP_Type(OP_Type), connection_handle(handle) {
		memset(&overlapped, 0, sizeof(OVERLAPPED));
	}
	Coro_IOCPOPData(Coro_IOCP_OPType OP_Type, std::shared_ptr<CoTimer::Handle> handle)
		: OP_Type(OP_Type), timer_handle(handle) {
		memset(&overlapped, 0, sizeof(OVERLAPPED));
	}
};

CoroutineScheduler::CoroutineScheduler()
	: _shouldshutdown(false), _isinitsuccess(false), _isrunning(false), _ExcuteEventProcessPool(4)
{
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
		std::thread IOEventLoop(&CoroutineScheduler::LoopSubmitIOEvent, this);
		std::thread EventLoop(&CoroutineScheduler::Loop, this);
		EventLoop.join();
		IOEventLoop.join();
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
					if (opdata->OP_Type == Coro_IOCP_OPType::OP_Connect)
					{
						int socket_error = 0;
						int error_len = sizeof(socket_error);

						if (!opdata->connection_handle)
							continue;

						if (getsockopt(opdata->connection_handle->socket, SOL_SOCKET, SO_ERROR,
							(char*)&socket_error, &error_len) == 0)
						{
							if (socket_error == 0)
							{
								if (ValidateConnectExResult(opdata->connection_handle->socket))
								{
									setsockopt(opdata->connection_handle->socket, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT,
										nullptr, 0);
									opdata->res = opdata->connection_handle->socket;
								}
								else
								{
									opdata->res = 0;
									if (opdata->connection_handle)
									{
										closesocket(opdata->connection_handle->socket);
										opdata->connection_handle->socket = 0;
									}
								}
							}
							else
							{
								opdata->res = 0;
								if (opdata->connection_handle)
								{
									closesocket(opdata->connection_handle->socket);
									opdata->connection_handle->socket = 0;
								}
							}
						}
						else
						{
							opdata->res = 0;
							//opdata->error_code = WSAGetLastError();
							if (opdata->connection_handle)
							{
								closesocket(opdata->connection_handle->socket);
								opdata->connection_handle->socket = 0;
							}
						}
					}

					else if (opdata->OP_Type == Coro_IOCP_OPType::OP_TimeOut)
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
								handle->coroutine = nullptr;
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
								handle->coroutine = nullptr;
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
			SubmitConnectEvent(opdata);
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

std::shared_ptr<CoTimer::Handle> CoroutineScheduler::create_timer(std::chrono::milliseconds interval)
{
	auto handle = std::make_shared<CoTimer::Handle>();

	auto timer = TimerTask::CreateOnce("", interval.count(), [handle, this]()->void {
		Coro_IOCPOPData* opdata = new Coro_IOCPOPData(Coro_IOCP_OPType::OP_TimeOut, handle);

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

	Coro_IOCPOPData* opdata = new Coro_IOCPOPData(Coro_IOCP_OPType::OP_Coroutine, handle);
	_optaskqueue.enqueue(opdata);
	_IOEventCV.notify_one();

	return handle;
}

std::shared_ptr<CoConnection::Handle> CoroutineScheduler::create_connection(BaseSocket socket, sockaddr_in localaddr, sockaddr_in remoteaddr)
{
	auto handle = std::make_shared<CoConnection::Handle>();
	handle->socket = socket;
	handle->localaddr = localaddr;
	handle->remoteaddr = remoteaddr;

	if (bind(socket, (sockaddr*)&localaddr, sizeof(localaddr)) == SOCKET_ERROR)
	{
		CoCloseSocket(socket);
		handle->active = false;
		handle->socket = 0;
		return handle;
	}

	Coro_IOCPOPData* opdata = new Coro_IOCPOPData(Coro_IOCP_OPType::OP_Connect, handle);
	_optaskqueue.enqueue(opdata);
	_IOEventCV.notify_one();

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

bool CoroutineScheduler::SubmitConnectEvent(Coro_IOCPOPData* opdata)
{
	if (!opdata->connection_handle)
		return false;

	if (!ConnectPtrNeed(opdata->connection_handle->socket))
		return false;

	if (!AssociateSocketWithIOCP((HANDLE)opdata->connection_handle->socket, (ULONG_PTR)opdata->connection_handle->socket))
		return false;

	DWORD bytesSent = 0;
	BOOL result = ConnectExPtr(
		opdata->connection_handle->socket,
		(sockaddr*)&opdata->connection_handle->remoteaddr,
		sizeof(opdata->connection_handle->remoteaddr),
		nullptr,
		0,
		&opdata->res,
		&opdata->overlapped
	);

	if (result == SOCKET_ERROR) {
		int error = WSAGetLastError();
		if (error != WSA_IO_PENDING) {
			return false;
		}
	}

	return true;
}

bool CoroutineScheduler::AssociateSocketWithIOCP(HANDLE handle, ULONG_PTR completionKey)
{
	HANDLE iocp = _iocp;
	if (iocp == NULL)
		return false;

	HANDLE hResult = CreateIoCompletionPort(
		handle,
		iocp,
		completionKey,
		0
	);

	bool success = (hResult == iocp);
	return success;
}