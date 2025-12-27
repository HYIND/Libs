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

struct TimerTaskWeakWrapper
{
    int fd;
    std::weak_ptr<TimerTask> weakData;

    TimerTaskWeakWrapper(int fd, const std::shared_ptr<TimerTask> &data)
        : fd(fd), weakData(data) {}
};

bool add_to_epoll(int epollfd, int fd, TimerTaskWeakWrapper *wrapper)
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
    bool Register_Task(std::shared_ptr<TimerTask> task);
    bool Cancel_Task(std::shared_ptr<TimerTask> task);

private:
    void Loop();
    int EventProcess(std::shared_ptr<TimerTask> &task, uint32_t events);

private:
    std::atomic<bool> _isrunning;
    int _epoll = epoll_create(1000);
    epoll_event _events[1500];

    CriticalSectionLock _lock;
    BiDirectionalMap<int, std::shared_ptr<TimerTask>> _timers;
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
        TimerTaskWeakWrapper *wrapper = new TimerTaskWeakWrapper(shutdown_eventfd, std::shared_ptr<TimerTask>(nullptr));
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

bool TimerProcessImpl::Register_Task(std::shared_ptr<TimerTask> task)
{
    if (!Running() || !task || !task->is_valid())
        return false;

    int timer_fd = task->timer_fd();
    if (timer_fd < 0)
        return false;

    TimerTaskWeakWrapper *wrapper = new TimerTaskWeakWrapper(timer_fd, std::shared_ptr<TimerTask>(task));
    LockGuard lock(_lock);
    if (!add_to_epoll(_epoll, timer_fd, wrapper))
    {
        std::cerr << "[TimerManager] Failed to add timerfd for '"
                  << task->name() << "'" << std::endl;
        return false;
    }

    _timers.Insert(timer_fd, task);

    // std::cout << "[TimerManager] Registered timer '" << task->name() << "'" << std::endl;
    return true;
}

bool TimerProcessImpl::Cancel_Task(std::shared_ptr<TimerTask> task)
{
    if (!task)
        return false;

    int timer_fd = task->timer_fd();
    if (timer_fd < 0)
        return false;

    LockGuard lock(_lock);

    std::shared_ptr<TimerTask> temp;
    if (!_timers.FindByLeft(timer_fd, temp))
        return false;

    remove_from_epoll(_epoll, timer_fd);
    _timers.EraseByLeft(timer_fd);
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
                auto *wrapper = static_cast<TimerTaskWeakWrapper *>(event.data.ptr);
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
                    std::shared_ptr<TimerTask> task = wrapper->weakData.lock();
                    std::shared_ptr<TimerTask> find_task;
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

int TimerProcessImpl::EventProcess(std::shared_ptr<TimerTask> &task, uint32_t events)
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
            (task->callback())();
    }
    catch (const std::exception &e)
    {
        std::cerr << "[TimerManager] Callback exception for '"
                  << task->name() << "': " << e.what() << std::endl;
    }

    // 如果是单次定时器，移除
    if (!task->is_repeat())
    {
        Cancel_Task(task);
        task->mark_invalid();
    }
    return 1;
}

class TimerProcess
{
public:
    static TimerProcess *Instance();

    bool Register_Task(std::shared_ptr<TimerTask> task);
    bool Cancel_Task(std::shared_ptr<TimerTask> task);

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
bool TimerProcess::Register_Task(std::shared_ptr<TimerTask> task)
{
    return pImpl->Register_Task(task);
};
bool TimerProcess::Cancel_Task(std::shared_ptr<TimerTask> task)
{
    return pImpl->Cancel_Task(task);
};


TimerTask::TimerTask(const std::string &name,
                     uint64_t delay_ms,
                     bool repeat,
                     Callback callback,
                     uint64_t interval_ms)
    : name_(name), interval_ms_(interval_ms), repeat_(repeat), valid_(false), callback_(std::move(callback)), timer_fd_(-1), delay_ms_(delay_ms)
{

    if (create_timer_fd())
        valid_ = true;
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

    return std::shared_ptr<TimerTask>(new TimerTask(name, delay_ms, false, std::move(callback)));
}

std::shared_ptr<TimerTask> TimerTask::CreateRepeat(
    const std::string &name,
    uint64_t interval_ms,
    Callback callback,
    uint64_t delay_ms)
{

    return std::shared_ptr<TimerTask>(new TimerTask(name, delay_ms, true, std::move(callback), interval_ms));
}

bool TimerTask::Run()
{
    return TimerProcess::Instance()->Register_Task(shared_from_this());
}

bool TimerTask::Stop()
{
    return TimerProcess::Instance()->Cancel_Task(shared_from_this());
}

bool TimerTask::create_timer_fd()
{
    timer_fd_ = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (timer_fd_ < 0)
    {
        return false;
    }

    itimerspec its;
    memset(&its, 0, sizeof(its));
    its.it_value.tv_sec = delay_ms_ / 1000;
    its.it_value.tv_nsec = (delay_ms_ % 1000) * 1000000;
    its.it_interval.tv_sec = repeat_ ? (interval_ms_ / 1000) : 0;
    its.it_interval.tv_nsec = repeat_ ? ((interval_ms_ % 1000) * 1000000) : 0;

    if (timerfd_settime(timer_fd_, 0, &its, nullptr) < 0)
    {
        close(timer_fd_);
        timer_fd_ = -1;
        return false;
    }

    return true;
}

void TimerTask::Clean()
{
    if (valid_)
    {
        TimerProcess::Instance()->Cancel_Task(shared_from_this());
        valid_ = false;
    }

    if (timer_fd_ >= 0)
    {
        close(timer_fd_);
        timer_fd_ = -1;
    }
}
