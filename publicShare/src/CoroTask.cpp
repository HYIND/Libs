#include "Coroutine.h"
#include "CoroutineScheduler.h"

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
