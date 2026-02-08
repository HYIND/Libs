#include "Coroutine.h"

#ifdef __linux__
#include "CoroutineScheduler_Linux.h"
#else 
#include "CoroutineScheduler_Win.h"
#endif

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
