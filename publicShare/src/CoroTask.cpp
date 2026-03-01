#include "Coroutine.h"

#ifdef __linux__
#include "CoroutineScheduler_Linux.h"
#else 
#include "CoroutineScheduler_Win.h"
#endif

#include "SpinLock.h"

bool RegisterTaskAwaiter::await_ready()
{
	return false;
}

void RegisterTaskAwaiter::await_suspend(std::coroutine_handle<> coro)
{
	CoroutineScheduler::Instance()->RegisterTaskCoroutine(coro);
}

void RegisterTaskAwaiter::await_resume()
{
}

TaskHandle::TaskHandle(std::coroutine_handle<> coroutine)
	: coroutine(coroutine)
{
}
TaskHandle::~TaskHandle()
{
}

class DelayDelete_Task {
public:
	// promise_type 必须定义在类内部
	struct promise_type {
		// 1. 创建 Task 对象
		DelayDelete_Task get_return_object() {
			return DelayDelete_Task{
				std::coroutine_handle<promise_type>::from_promise(*this)
			};
		}

		// 2. 初始挂起 - 启动就挂起
		std::suspend_always initial_suspend() noexcept {
			return {};
		}

		// 3. 最终挂起 - 永远挂起，不自动销毁
		std::suspend_always final_suspend() noexcept {
			return {};
		}

		// 4. void 返回类型
		void return_void() {}

		// 5. 异常处理
		void unhandled_exception() {
			std::terminate();
		}
	};

	// 构造函数 - 从 handle 构造
	explicit DelayDelete_Task(std::coroutine_handle<promise_type> h)
		: handle_(h) {
	}

	// 析构函数 - 自动销毁协程
	~DelayDelete_Task() {
		if (handle_) {
			handle_.destroy();
		}
	}

	// 移动语义
	DelayDelete_Task(DelayDelete_Task&& other) noexcept
		: handle_(std::exchange(other.handle_, {})) {
	}

	DelayDelete_Task& operator=(DelayDelete_Task&& other) noexcept {
		if (this != &other) {
			if (handle_) {
				handle_.destroy();
			}
			handle_ = std::exchange(other.handle_, {});
		}
		return *this;
	}

	// 禁止拷贝
	DelayDelete_Task(const DelayDelete_Task&) = delete;
	DelayDelete_Task& operator=(const DelayDelete_Task&) = delete;

	// 获取 handle（用于对称转移）
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
			//std::cout << "调度器协程: while循环开始, 次数 = " << ++loop_count << std::endl;
			if (_res)
			{
				CoroutineScheduler::Instance()->DeleteTaskLater(_res);
				_res.reset();
			}
			_resume_spin.unlock();
			co_await std::suspend_always{};
		}
	}
};

DelayDelete_Task Delaydelete_Scheduler::_scheduler_handle = Delaydelete_Scheduler::scheduler_co();
SpinLock Delaydelete_Scheduler::_resume_spin = SpinLock();
std::shared_ptr<std::coroutine_handle<>> Delaydelete_Scheduler::_res = std::shared_ptr<std::coroutine_handle<>>(nullptr);

std::coroutine_handle<> DeleteLater(std::shared_ptr<std::coroutine_handle<>> p)
{
	if (!p || !*p)
		return std::noop_coroutine();

	Delaydelete_Scheduler::_resume_spin.lock();
	Delaydelete_Scheduler::_res = p;
	return Delaydelete_Scheduler::_scheduler_handle.get_handle();
}