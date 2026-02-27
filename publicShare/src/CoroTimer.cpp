#include "Coroutine.h"
#include <iostream>

#ifdef __linux__
#include "CoroutineScheduler_Linux.h"
#else
#include "CoroutineScheduler_Win.h"
#endif

CoTimer::Handle::Handle()
	: result(CoTimer::WakeType::RUNNING), active(true), corodone{ false }
{
}

CoTimer::Handle::~Handle()
{
	if (task)
	{
		task->Clean();
		task.reset();
	}
}

CoTimer::Awaiter::Awaiter(std::shared_ptr<CoTimer::Handle> handle) : handle(handle)
{
}

bool CoTimer::Awaiter::await_ready() const noexcept
{
	return !handle || !handle->active;
}

void CoTimer::Awaiter::await_suspend(std::coroutine_handle<> coro)
{

	auto trytoresume = [&]() -> void
		{
			bool expected = false;
			if (handle->corodone.compare_exchange_strong(expected, true))
				coro.resume();
		};

	if (!handle || !handle->active)
	{
		trytoresume();
		return;
	}

	if (!handle->active || coro.done())
	{
		trytoresume();
		return;
	}

	{
		LockGuard lock(handle->corolock);
		if (!handle->active || coro.done())
		{
			trytoresume();
			return;
		}
		handle->coroutine = coro;
	}
}

CoTimer::WakeType CoTimer::Awaiter::await_resume() noexcept
{
	if (handle)
		return handle->result;
	return CoTimer::WakeType::Error;
}

CoTimer::CoTimer(std::chrono::milliseconds timeout)
{
	handle = CoroutineScheduler::Instance()->create_timer(timeout);
	if (!handle)
		std::cerr << "CoTimer create_timer fail!";
}

CoTimer::CoTimer(CoTimer&& other) noexcept
{
	handle = other.handle;
	other.handle.reset();
}

CoTimer& CoTimer::operator=(CoTimer&& other) noexcept
{
	if (this != &other)
	{
		handle = other.handle;
		other.handle.reset();
	}
	return *this;
}

CoTimer::~CoTimer()
{
	wake();
}

// 协程等待操作
CoTimer::Awaiter CoTimer::operator co_await()
{
	return CoTimer::Awaiter(handle);
}

// 立即唤醒
void CoTimer::wake()
{
	if (handle)
		CoroutineScheduler::Instance()->wake_timer(handle);
}

Task<bool> CoSleep(std::chrono::milliseconds timeout)
{
	co_return co_await CoTimer(timeout) == CoTimer::WakeType::TIMEOUT;
}