#include "Coroutine.h"

#ifdef __linux__
#include "CoroutineScheduler_Linux.h"
#else 
#include "CoroutineScheduler_Win.h"
#endif

#include "SpinLock.h"

bool yieldAwaiter::await_ready()
{
	return false;
}

void yieldAwaiter::await_suspend(std::coroutine_handle<> coro)
{
	CoroutineScheduler::Instance()->RegisterTaskCoroutine(coro);
}

void yieldAwaiter::await_resume()
{
}

yieldAwaiter yield::operator co_await()
{
	return {};
}

TaskHandle::TaskHandle(std::coroutine_handle<> coroutine)
	: coroutine(coroutine)
{
}
TaskHandle::~TaskHandle()
{
}

//最小化协程
class DelayDelete_Task {
public:
	struct promise_type {
		DelayDelete_Task get_return_object() {
			return DelayDelete_Task{
				std::coroutine_handle<promise_type>::from_promise(*this)
			};
		}

		std::suspend_never initial_suspend() noexcept { return {}; }	// 启动不挂起	
		std::suspend_never final_suspend() noexcept { return {}; }// 最终不挂起

		void return_void() {}
		void unhandled_exception() {}
	};

	explicit DelayDelete_Task(std::coroutine_handle<promise_type> h) : handle_(h) {}
	~DelayDelete_Task() {
		if (handle_) {
			handle_.destroy();
		}
	}

	DelayDelete_Task(DelayDelete_Task&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}

	DelayDelete_Task& operator=(DelayDelete_Task&& other) noexcept {
		if (this != &other) {
			if (handle_) {
				handle_.destroy();
			}
			handle_ = std::exchange(other.handle_, {});
		}
		return *this;
	}

	DelayDelete_Task(const DelayDelete_Task&) = delete;
	DelayDelete_Task& operator=(const DelayDelete_Task&) = delete;

	std::coroutine_handle<> get_handle() const {
		return handle_;
	}

private:
	std::coroutine_handle<promise_type> handle_;
};

constexpr size_t Delete_Worker_Count = 4;
class Delaydelete_Scheduler
{
	struct Worker
	{
		SpinLock _resume_spin;
		std::shared_ptr<std::coroutine_handle<>> _res;
		DelayDelete_Task _scheduler_handle;

		Worker(size_t index) :_scheduler_handle(Delaydelete_Scheduler::scheduler_co(index)) {}
	};
public:
	static std::vector<Worker*> workers;
	static std::atomic<bool> isInitStart;
	static std::atomic<bool> isInitDone;
	static size_t _current_worker;

public:
	static DelayDelete_Task scheduler_co(size_t index)
	{
		while (true)
		{
			co_await std::suspend_always{};
			Delaydelete_Scheduler::workers[index]->_res.reset();
			Delaydelete_Scheduler::workers[index]->_resume_spin.unlock();
		}
	}

	static void Initialize()
	{
		for (int i = 0; i < Delete_Worker_Count; i++)
		{
			auto worker = new Worker(i);
			workers.push_back(worker);
		}
	}

	static std::coroutine_handle<> RequestDelete(std::shared_ptr<std::coroutine_handle<>> p)
	{
		bool expected = false;
		if (isInitStart.compare_exchange_strong(expected, true))
		{
			Initialize();
			isInitDone.store(true);
		}

		while (!isInitDone.load()) {}

		size_t idx = _current_worker++ % Delete_Worker_Count;

		workers[idx]->_resume_spin.lock();
		workers[idx]->_res = std::move(p);
		return workers[idx]->_scheduler_handle.get_handle();
	}
};

std::vector<Delaydelete_Scheduler::Worker*> Delaydelete_Scheduler::workers = {};
std::atomic<bool> Delaydelete_Scheduler::isInitStart{ false };
std::atomic<bool> Delaydelete_Scheduler::isInitDone{ false };
size_t Delaydelete_Scheduler::_current_worker = 0;

std::coroutine_handle<> DeleteLater(std::shared_ptr<std::coroutine_handle<>> p)
{
	return Delaydelete_Scheduler::RequestDelete(std::move(p));
}