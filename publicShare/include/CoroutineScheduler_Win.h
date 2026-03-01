#pragma once

#include "Coroutine.h"
#include "ThreadPool.h"

struct Coro_IOCPOPData;

class AsyncConnector
{
public:
	struct ConnectEvent
	{
		SOCKET s;
		int error;
		void* userdata = nullptr;
	};


public:
	AsyncConnector();
	~AsyncConnector();

	void Connect(SOCKET s, const std::string& ip, const int port, void* userdata);
	bool GetDoneEvents(std::vector<ConnectEvent>& events);
	void Cleanup();

private:
	bool GetEvent(ConnectEvent& event);

private:
	struct SocketContext {
		SOCKET s;
		WSAEVENT hEvent;
		std::string address;
		int port;
		void* userdata = nullptr;
	};

	SafeArray<SocketContext> contexts;
};

class CoroutineScheduler
{
public:
	static CoroutineScheduler* Instance();
	int Run();
	void Stop();
	bool Running();

public:	//DelayDelete
	void DeleteTaskLater(std::shared_ptr<std::coroutine_handle<>> shared);

public: // Timer
	std::shared_ptr<CoTimer::Handle> create_timer(std::chrono::milliseconds interval);
	void wake_timer(std::shared_ptr<CoTimer::Handle> weakhandle);

public: // Task
	std::shared_ptr<TaskHandle> RegisterTaskCoroutine(std::coroutine_handle<> coroutine);

public: // Connect
	std::shared_ptr<CoConnection::Handle> create_connection(BaseSocket socket, const std::string& IP, int port);

private:
	CoroutineScheduler();
	~CoroutineScheduler();
	void LoopCoTaskRelease();
	void LoopSubmitIOEvent();
	void Loop();
	bool GetDoneIOEvents(std::vector<Coro_IOCPOPData*>& opdatas);
	int EventProcess(Coro_IOCPOPData* opdata);
	bool AddReadShutDownEvent(Coro_IOCPOPData* opdata);
	void DoPostIOEvents(std::vector<Coro_IOCPOPData*>& opdatas);

private:
	bool SubmitTimeOutEvent(Coro_IOCPOPData* opdata);
	bool SubmitCoroutineEvent(Coro_IOCPOPData* opdata);

	template <typename Callable>
	void ExcuteCoroutine(Callable&& callable)
	{
		_ExcuteEventProcessPool.submit([callable = std::forward<Callable>(callable)]() mutable
			{
				try
				{
					std::atomic_thread_fence(std::memory_order_acquire);
					callable();
					std::atomic_thread_fence(std::memory_order_release);
				}
				catch (const std::exception& e)
				{
					std::cerr << "ExcuteCoroutine task Error: " << e.what() << '\n';
				} });
	}

private:
	bool _shouldshutdown;
	bool _isrunning;
	bool _isinitsuccess;

	HANDLE _iocp;

	SafeQueue<Coro_IOCPOPData*> _optaskqueue;

	ThreadPool _ExcuteEventProcessPool;

	std::mutex _IOEventLock;
	std::condition_variable _IOEventCV;

	CriticalSectionLock _doPostIOEventLock;

	std::unique_ptr<AsyncConnector> _asyncConnector;

	std::mutex _CoTaskReleaseLock;
	std::condition_variable _CoTaskReleaseCV;
	SafeQueue<std::shared_ptr<std::coroutine_handle<>>> _pendingReleaseTasks;
};
