#include "Coroutine.h"
#include "CoroutineScheduler.h"

CoConnection::Handle::Handle()
    : fd(-1), active(true), corodone{false}, res(-1)
{
}

CoConnection::Handle::~Handle()
{
}

CoConnection::Awaiter::Awaiter(std::shared_ptr<Handle> handle)
    : handle(handle)
{
}

bool CoConnection::Awaiter::await_ready()
{
    return !handle || !handle->active;
}

void CoConnection::Awaiter::await_suspend(std::coroutine_handle<> coro)
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

int CoConnection::Awaiter::await_resume()
{
    return handle->res;
}

CoConnection::CoConnection(int fd, sockaddr_in addr)
{
    handle = CoroutineScheduler::Instance()->create_connection(fd, addr);
}

CoConnection::~CoConnection() {}

CoConnection::Awaiter CoConnection::operator co_await()
{
    return CoConnection::Awaiter(handle);
}