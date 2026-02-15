#include "Timer.h"
#include <atomic>
#include <iostream>
#include <thread>
#include <string.h>
#include "BiDirectionalMap.h"
#include "CriticalSectionLock.h"
#include "TimeWheel.h"

class HierarchicalTimeWheel;

class TimerProcess
{
public:
    static TimerProcess* Instance();

    bool Register_Task(std::shared_ptr<TimerTaskHandle> task);
    bool Cancel_Task(std::shared_ptr<TimerTaskHandle> task);
    bool Wake_Task(std::shared_ptr<TimerTaskHandle> task);

    TimerProcess(const TimerProcess&) = delete;
    TimerProcess& operator=(const TimerProcess&) = delete;

private:
    void Start();
    void Stop();
    bool Running();
    TimerProcess();

private:
    std::unique_ptr<HierarchicalTimeWheel> pImpl;
};

TimerProcess* TimerProcess::Instance()
{
    static TimerProcess* m_instance = new TimerProcess();
    return m_instance;
}
TimerProcess::TimerProcess()
{
    pImpl = std::make_unique<HierarchicalTimeWheel>();
    Start();
}
void TimerProcess::Start()
{
    if (!pImpl->Running())
    {
        pImpl->Start();
    }
}
void TimerProcess::Stop()
{
    if (pImpl->Running())
    {
        pImpl->Stop();
    }
};
bool TimerProcess::Running()
{
    return pImpl->Running();
};
bool TimerProcess::Register_Task(std::shared_ptr<TimerTaskHandle> task)
{
    return pImpl->Register_Task(task);
};
bool TimerProcess::Cancel_Task(std::shared_ptr<TimerTaskHandle> task)
{
    return pImpl->Cancel_Task(task);
};
bool TimerProcess::Wake_Task(std::shared_ptr<TimerTaskHandle> task)
{
    return pImpl->Wake_Task(task);
}


TimerTask::TimerTask(const std::string &name,
                     uint64_t delay_ms,
                     bool repeat,
                     Callback callback,
                     uint64_t interval_ms)
{
    _handle = std::make_shared<TimerTaskHandle>();
    _handle->name_ = name;
    _handle->interval_ms_ = interval_ms;
    _handle->repeat_ = repeat;
    _handle->mark_invalid();
    _handle->callback_ = std::move(callback);
    _handle->delay_ms_ = delay_ms;
}

TimerTask::~TimerTask()
{
}

std::shared_ptr<TimerTask> TimerTask::CreateOnce(
    const std::string &name,
    uint64_t delay_ms,
    Callback callback)
{
    if (delay_ms <= 5)
        delay_ms = 5;
    return std::shared_ptr<TimerTask>(new TimerTask(name, delay_ms, false, std::move(callback)));
}

std::shared_ptr<TimerTask> TimerTask::CreateRepeat(
    const std::string &name,
    uint64_t interval_ms,
    Callback callback,
    uint64_t delay_ms)
{

    if (delay_ms <= 5)
        delay_ms = 5;
    return std::shared_ptr<TimerTask>(new TimerTask(name, delay_ms, true, std::move(callback), interval_ms));
}

bool TimerTask::Run()
{
    return TimerProcess::Instance()->Register_Task(_handle);
}

bool TimerTask::Stop()
{
    return TimerProcess::Instance()->Cancel_Task(_handle);
}

bool TimerTask::Wake()
{
    return TimerProcess::Instance()->Wake_Task(_handle);
}

void TimerTask::Clean()
{
    if (!_handle)
        return;

    if (_handle->valid_)
    {
        Stop();
        _handle->mark_invalid();
    }
}
