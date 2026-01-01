#include "Coroutine.h"
#include "CoroutineScheduler.h"
#include <iostream>

CoTimer::Handle::Handle()
    : result(CoTimer::WakeType::RUNNING), fd(-1), active(true), corodone{false}
{
}

CoTimer::Handle::~Handle()
{
    if (fd > 0)
    {
        close(fd);
        fd = -1;
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
    {
        uint64_t value;
        ssize_t ret = read(handle->fd, &value, sizeof(value));
        return handle->result;
    }
    return CoTimer::WakeType::Error;
}

CoTimer::CoTimer(std::chrono::milliseconds timeout, bool periodic)
{
    handle = CoroutineScheduler::Instance()->create_timer(timeout);
    if (!handle)
        std::cerr << "CoTimer create_timer fail!";
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