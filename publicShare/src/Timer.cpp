#include "Timer.h"
#include <sys/timerfd.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <atomic>
#include <iostream>
#include <thread>
#include <string.h>
#include "BiDirectionalMap.h"
#include "CriticalSectionLock.h"

#define SAFE_DELETE(x) \
    if (x)             \
    {                  \
        delete x;      \
        x = nullptr;   \
    }

struct EpollWeakWrapper
{
    int fd;
    std::weak_ptr<TimerTaskHandle> weakData;

    EpollWeakWrapper(int fd, const std::shared_ptr<TimerTaskHandle> &data)
        : fd(fd), weakData(data) {}
};

struct TimerTaskHandle
{
    using Callback = std::function<void()>;

    std::string name_;
    uint64_t interval_ms_;
    bool repeat_;
    bool valid_;
    Callback callback_;
    int timer_fd_;
    uint64_t delay_ms_;

    const std::string &name() const { return name_; }
    uint64_t interval() const { return interval_ms_; }
    bool is_repeat() const { return repeat_; }
    bool is_valid() const { return valid_; }
    int timer_fd() const { return timer_fd_; }
    Callback callback() { return callback_; };
    void mark_invalid() { valid_ = false; }
    void mark_valid() { valid_ = true; }
};

bool add_to_epoll(int epollfd, int fd, EpollWeakWrapper *wrapper)
{
    epoll_event event;
    memset(&event, 0, sizeof(event));
    event.events = EPOLLIN | EPOLLET;
    event.data.ptr = wrapper;
    return epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event) == 0;
}

bool remove_from_epoll(int epollfd, int fd)
{
    return epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL) == 0;
}

static int shutdown_eventfd = -1;

class TimerProcessImpl
{
public:
    TimerProcessImpl();
    int Run();
    void Stop();
    bool Running();

public:
    bool Register_Task(std::shared_ptr<TimerTaskHandle> task);
    bool Cancel_Task(std::shared_ptr<TimerTaskHandle> task);

private:
    void Loop();
    int EventProcess(std::shared_ptr<TimerTaskHandle> &task, uint32_t events);

private:
    std::atomic<bool> _isrunning;
    int _epoll = epoll_create(1000);
    epoll_event _events[1500];

    CriticalSectionLock _lock;
    SafeBiDirectionalMap<int, std::shared_ptr<TimerTaskHandle>> _timers;
};

TimerProcessImpl::TimerProcessImpl()
    : _isrunning(false), _epoll(-1)
{
    if (shutdown_eventfd < 0)
    {
        shutdown_eventfd = eventfd(0, EFD_NONBLOCK);
        if (shutdown_eventfd < 0)
        {
            perror("shutdown_eventfd init error!");
        }
    }
    _epoll = epoll_create(1000);
    if (_epoll < 0)
    {
        perror("epollfd init error!");
    }
    if (shutdown_eventfd > 0 && _epoll > 0)
    {
        EpollWeakWrapper *wrapper = new EpollWeakWrapper(shutdown_eventfd, std::shared_ptr<TimerTaskHandle>(nullptr));
        add_to_epoll(_epoll, shutdown_eventfd, wrapper);
    }
}

int TimerProcessImpl::Run()
{
    try
    {
        if (_epoll < 0)
        {
            std::cerr << "invalid epollfd!\n";
            return -1;
        }
        if (shutdown_eventfd > 0 && _epoll > 0)
        {
            uint64_t value = 0;
            ssize_t n = read(shutdown_eventfd, &value, sizeof(value));
        }

        _isrunning = true;
        std::thread EventLoop(&TimerProcessImpl::Loop, this);
        EventLoop.join();
        _isrunning = false;

        // ThreadEnd();
        return 1;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return -1;
    }
}

void TimerProcessImpl::Stop()
{
    if (shutdown_eventfd < 0)
        return;
    uint64_t num = 1;
    write(shutdown_eventfd, &num, sizeof(uint64_t));
}

bool TimerProcessImpl::Running()
{
    return _isrunning;
}

bool TimerProcessImpl::Register_Task(std::shared_ptr<TimerTaskHandle> handle)
{
    if (!Running() || !handle)
        return false;

    int timer_fd = handle->timer_fd();
    if (timer_fd < 0)
        return false;

    EpollWeakWrapper *wrapper = new EpollWeakWrapper(timer_fd, handle);
    LockGuard lock(_lock);
    if (!add_to_epoll(_epoll, timer_fd, wrapper))
    {
        std::cerr << "[TimerManager] Failed to add timerfd for ["
                  << handle->name() << "]" << std::endl;
        return false;
    }
    handle->mark_valid();
    _timers.Insert(timer_fd, handle);
    // std::cout << "[TimerManager] Registered timer [" << handle->name() << "]" << std::endl;
    return true;
}

bool TimerProcessImpl::Cancel_Task(std::shared_ptr<TimerTaskHandle> handle)
{
    if (!handle)
        return false;

    int timer_fd = handle->timer_fd();
    if (timer_fd < 0)
        return false;

    LockGuard lock(_lock);

    std::shared_ptr<TimerTaskHandle> temp;
    if (!_timers.FindByLeft(timer_fd, temp))
        return false;

    remove_from_epoll(_epoll, timer_fd);
    _timers.EraseByLeft(timer_fd);
    temp->mark_invalid();
    // std::cout << "[TimerManager] Unregistered timer '" << task->name() << "'" << std::endl;

    return true;
}

void TimerProcessImpl::Loop()
{
    std::cout << "TimerProcess , EventLoop\n";

    bool shouldshutdown = false;
    while (_isrunning)
    {
        if (shouldshutdown)
            break;

        static int maxevents = 1000;
        epoll_event _events[maxevents];
        int number = epoll_wait(_epoll, _events, maxevents, 100);
        if (number < 0 && (errno != EINTR))
        {
            std::cout << "TimerProcess epoll failure\n";
            break;
        }

        for (int i = 0; i < number; i++)
        {
            try
            {
                epoll_event event = _events[i];
                auto *wrapper = static_cast<EpollWeakWrapper *>(event.data.ptr);
                if (wrapper)
                {
                    if (shutdown_eventfd > 0 && wrapper->fd == shutdown_eventfd)
                    {
                        uint64_t num;
                        read(shutdown_eventfd, &num, sizeof(uint64_t));
                        shouldshutdown = true;
                        std::cout << "TimerProcess , EventLoop closing......\n";
                        continue;
                    }

                    LockGuard lock(_lock);
                    std::shared_ptr<TimerTaskHandle> task = wrapper->weakData.lock();
                    std::shared_ptr<TimerTaskHandle> find_task;
                    if (task && _timers.FindByLeft(task->timer_fd(), find_task) && task == find_task && task->is_valid())
                    {
                        EventProcess(task, event.events);
                    }
                    else
                    {
                        SAFE_DELETE(wrapper);
                    }
                }
            }
            catch (const std::exception &e)
            {
                std::cerr << "EventLoop unknown exception:" << e.what() << '\n';
            }
        }
    }
}

int TimerProcessImpl::EventProcess(std::shared_ptr<TimerTaskHandle> &task, uint32_t events)
{
    // 读取过期次数
    uint64_t expirations;
    if (read(task->timer_fd(), &expirations, sizeof(expirations)) != sizeof(expirations))
        return -1;

    if (!task || !task->is_valid())
        return -1;

    // 执行回调
    try
    {
        if (task->callback())
            std::invoke(task->callback());
    }
    catch (const std::exception &e)
    {
        std::cerr << "[Timer] Callback exception for timername '"
                  << task->name() << "': " << e.what() << std::endl;
    }

    // 如果是单次定时器，移除
    if (!task->is_repeat())
    {
        Cancel_Task(task);
    }
    return 1;
}

class TimerProcess
{
public:
    static TimerProcess *Instance();

    bool Register_Task(std::shared_ptr<TimerTaskHandle> task);
    bool Cancel_Task(std::shared_ptr<TimerTaskHandle> task);

    TimerProcess(const TimerProcess &) = delete;
    TimerProcess &operator=(const TimerProcess &) = delete;

private:
    void Start(bool isBlock);
    void Stop();
    bool Running();
    TimerProcess();

private:
    std::unique_ptr<TimerProcessImpl> pImpl;
};

TimerProcess *TimerProcess::Instance()
{
    static TimerProcess *m_instance = new TimerProcess();
    return m_instance;
}
TimerProcess::TimerProcess()
{
    pImpl = std::make_unique<TimerProcessImpl>();
    Start(false);
}
void TimerProcess::Start(bool isBlock)
{
    if (!pImpl->Running())
    {
        std::thread CoreThread(&TimerProcessImpl::Run, pImpl.get());
        if (isBlock)
            CoreThread.join();
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            CoreThread.detach();
        }
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
    _handle->timer_fd_ = -1;
    _handle->delay_ms_ = delay_ms;
    create_timer_fd();
}

TimerTask::~TimerTask()
{
    Clean();
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

bool TimerTask::create_timer_fd()
{
    _handle->timer_fd_ = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (_handle->timer_fd_ < 0)
    {
        return false;
    }

    itimerspec its;
    memset(&its, 0, sizeof(its));
    its.it_value.tv_sec = _handle->delay_ms_ / 1000;
    its.it_value.tv_nsec = (_handle->delay_ms_ % 1000) * 1000000;
    its.it_interval.tv_sec = _handle->repeat_ ? (_handle->interval_ms_ / 1000) : 0;
    its.it_interval.tv_nsec = _handle->repeat_ ? ((_handle->interval_ms_ % 1000) * 1000000) : 0;

    if (timerfd_settime(_handle->timer_fd_, 0, &its, nullptr) < 0)
    {
        close(_handle->timer_fd_);
        _handle->timer_fd_ = -1;
        return false;
    }

    return true;
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

    if (_handle->timer_fd_ >= 0)
    {
        close(_handle->timer_fd_);
        _handle->timer_fd_ = -1;
    }
}
