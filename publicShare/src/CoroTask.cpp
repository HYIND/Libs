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

class Delaydelete_Scheduler {

public:
	static DelayDelete_Task _scheduler_handle;
	static SpinLock _resume_spin;
	static std::shared_ptr<std::coroutine_handle<>> _res;

public:
	static DelayDelete_Task scheduler_co()
	{
		int loop_count = 0;
		while (true) {
			co_await std::suspend_always{};
			_res.reset();
			_resume_spin.unlock();
		}
	}
	static std::coroutine_handle<> RequestDelete(std::shared_ptr<std::coroutine_handle<>> p)
	{
		_resume_spin.lock();
		_res = std::move(p);
		return _scheduler_handle.get_handle();
	}
};

DelayDelete_Task Delaydelete_Scheduler::_scheduler_handle = Delaydelete_Scheduler::scheduler_co();
SpinLock Delaydelete_Scheduler::_resume_spin = SpinLock();
std::shared_ptr<std::coroutine_handle<>> Delaydelete_Scheduler::_res = std::shared_ptr<std::coroutine_handle<>>(nullptr);

std::coroutine_handle<> DeleteLater(std::shared_ptr<std::coroutine_handle<>> p)
{
	return Delaydelete_Scheduler::RequestDelete(std::move(p));
}