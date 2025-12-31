#include <sys/eventfd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <liburing.h>
#include "ThreadPool.h"
#include "Coroutine.h"
#include "string.h"
#include <sys/timerfd.h>
#include <unistd.h>

#define SAFE_DELETE(x) \
    if (x)             \
    {                  \
        delete x;      \
        x = nullptr;   \
    }

#define ENTRIES 3000

static int shutdown_eventfd = -1;

class CoroutineScheduler;

enum class IOUring_OPType
{
    OP_TimeOut = 1,   // 定时器
    OP_Coroutine = 2, // 协程执行
    OP_Connect = 3,   // socket connect
    OP_SHUTDOWN
};
struct IOuringOPData
{
    IOUring_OPType OP_Type;
    int fd = 0;
    char buffer[sizeof(uint64_t)];
    std::coroutine_handle<> coroutine;
    sockaddr_in addr;
    int *res = nullptr;
    IOuringOPData(IOUring_OPType OP_Type, int fd)
        : OP_Type(OP_Type), fd(fd) {}
    IOuringOPData(IOUring_OPType OP_Type, std::coroutine_handle<> coroutine)
        : OP_Type(OP_Type), coroutine(coroutine) {}
};

class CoroutineScheduler
{
public:
    static CoroutineScheduler *Instance();
    int Run();
    void Stop();
    bool Running();

public: // Timer
    std::shared_ptr<CoTimer::Handle> create_timer(std::chrono::milliseconds interval);
    void wake_timer(std::shared_ptr<CoTimer::Handle> weakhandle);
    bool RegisterTimerCoroutine(std::shared_ptr<CoTimer::Handle> handlehandle, std::coroutine_handle<> coroutine);

public: // Task
    void RegisterTaskCoroutine(std::coroutine_handle<> coroutine);
    void UnBindTaskCoroutineThread(std::coroutine_handle<> coroutine);

public: // Connect
    void RegisterConnectCoroutine(int fd, sockaddr_in addr, int *res, std::coroutine_handle<> coroutine);

private:
    CoroutineScheduler();
    void LoopSubmitIOEvent();
    void Loop();
    bool GetDoneIOEvents(std::vector<IOuringOPData *> &opdatas);
    int EventProcess(IOuringOPData *opdata);
    bool AddReadShutDownEvent(IOuringOPData *opdata);
    void DoPostIOEvents(std::vector<IOuringOPData *> &opdatas);

private:
    bool SubmitTimerEvent(IOuringOPData *opdata);
    bool SubmitCoroutineEvent(IOuringOPData *opdata);
    bool SubmitConnectEvent(IOuringOPData *opdata);

    template <typename Callable>
    void ExcuteCoroutine(Callable &&callable)
    {
        _ExcuteEventProcessPool.submit([callable = std::forward<Callable>(callable)]() mutable
                                       {
            try
            {
                std::atomic_thread_fence(std::memory_order_acquire);
                callable();
                std::atomic_thread_fence(std::memory_order_release);
            }
            catch (const std::exception &e)
            {
                std::cerr << "ExcuteCoroutine task Error: " << e.what() << '\n';
            } });
    }

private:
    bool _shouldshutdown;
    bool _isrunning;
    bool _isinitsuccess;
    io_uring ring;

    SafeQueue<IOuringOPData *> _optaskqueue;

    ThreadPool _ExcuteEventProcessPool;

    std::mutex _IOEventLock;
    std::condition_variable _IOEventCV;

    CriticalSectionLock _doPostIOEventLock;

    SafeMap<int, std::shared_ptr<CoTimer::Handle>> _timers;

    // std::unordered_map<std::coroutine_handle<>, uint32_t> _coroutine_thread_map; // 协程与线程绑定
    // CriticalSectionLock _coroutine_thread_map_lock;
};

CoTimer::Awaiter::Awaiter(std::shared_ptr<CoTimer::Handle> handle) : handle(handle)
{
}

bool CoTimer::Awaiter::await_ready() const noexcept
{
    return !handle || !handle->active;
}

void CoTimer::Awaiter::await_suspend(std::coroutine_handle<> corohandle)
{
    if (!handle || !handle->active)
    {
        // 定时器过期，立即恢复协程
        bool expected = false;
        if (handle->corodone.compare_exchange_strong(expected, true))
            corohandle.resume();
        return;
    }

    // 注册协程到定时器
    // 注册失败立即恢复协程
    if (!CoroutineScheduler::Instance()->RegisterTimerCoroutine(handle, corohandle))
    {
        bool expected = false;
        if (handle->corodone.compare_exchange_strong(expected, true))
            corohandle.resume();
        return;
    }
}

CoTimer::WakeType CoTimer::Awaiter::await_resume() noexcept
{
    if (handle)
    {
        uint64_t value;
        ssize_t ret = read(handle->timer_fd, &value, sizeof(value));
        return handle->wakeresult;
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

CoConnection::Awaiter::Awaiter(int fd, sockaddr_in addr)
    : fd(fd), addr(addr), res(-1)
{
}

bool CoConnection::Awaiter::await_ready()
{
    return false;
}

void CoConnection::Awaiter::await_suspend(std::coroutine_handle<> coro)
{
    CoroutineScheduler::Instance()->RegisterConnectCoroutine(fd, addr, &res, coro);
}

int CoConnection::Awaiter::await_resume()
{
    return res;
}

CoroutineScheduler::CoroutineScheduler()
    : _shouldshutdown(false), _isinitsuccess(false), _isrunning(false), _ExcuteEventProcessPool(4)
{
    memset(&ring, 0, sizeof(ring));
    int ret = io_uring_queue_init(ENTRIES, &ring, 0);
    if (ret < 0)
    {
        _isinitsuccess = false;
        perror("io_uring_queue_init fail!");
    }
    _isinitsuccess = true;
    if (shutdown_eventfd < 0)
    {
        shutdown_eventfd = eventfd(0, EFD_NONBLOCK);
        if (shutdown_eventfd < 0)
        {
            perror("shutdown_eventfd init error!");
        }
    }

    if (!Running())
    {
        std::thread CoreThread(&CoroutineScheduler::Run, this);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        CoreThread.detach();
    }
}

int CoroutineScheduler::Run()
{
    try
    {
        _isrunning = true;
        _shouldshutdown = false;
        if (shutdown_eventfd > 0 && _isinitsuccess)
        {
            uint64_t value = 0;
            ssize_t n = read(shutdown_eventfd, &value, sizeof(value));

            IOuringOPData *opdata = new IOuringOPData(IOUring_OPType::OP_SHUTDOWN, shutdown_eventfd);
            opdata->fd = shutdown_eventfd;
            AddReadShutDownEvent(opdata);
        }
        _ExcuteEventProcessPool.start();
        std::thread IOEventLoop(&CoroutineScheduler::LoopSubmitIOEvent, this);
        std::thread EventLoop(&CoroutineScheduler::Loop, this);
        EventLoop.join();
        IOEventLoop.join();
        _ExcuteEventProcessPool.stop();
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

bool CoroutineScheduler::Running()
{
    return _isrunning;
}

void CoroutineScheduler::Stop()
{
    if (shutdown_eventfd < 0)
        return;
    uint64_t num = 1;
    write(shutdown_eventfd, &num, sizeof(uint64_t));
}

bool CoroutineScheduler::AddReadShutDownEvent(IOuringOPData *opdata)
{
    io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    if (!sqe)
        return false;

    io_uring_prep_read(sqe, opdata->fd, opdata->buffer, sizeof(uint64_t), 0);
    io_uring_sqe_set_data(sqe, (void *)opdata);

    int submitted = io_uring_submit(&ring);
    if (submitted < 0)
    {
        perror("shutdown_eventfd submit to io_uring error!");
        return false;
    }

    return true;
}

void CoroutineScheduler::LoopSubmitIOEvent()
{

    if (!_isrunning || _shouldshutdown)
        return;

    do
    {

        std::unique_lock<std::mutex> lock(_IOEventLock);
        _IOEventCV.wait_for(lock, std::chrono::milliseconds(50));

        if (_shouldshutdown || !_isrunning)
            break;

        std::vector<IOuringOPData *> opdatas;
        constexpr size_t BATCH_SIZE = 100;
        while (!_optaskqueue.empty())
        {
            IOuringOPData *temp = nullptr;
            for (size_t i = 0; i < BATCH_SIZE && _optaskqueue.dequeue(temp); ++i)
            {
                if (temp)
                    opdatas.push_back(temp);
            }
            if (!opdatas.empty())
            {
                DoPostIOEvents(opdatas);
                opdatas.clear();
            }
        }

    } while (_isrunning && !_shouldshutdown);
}

void CoroutineScheduler::Loop()
{
    std::cout << "CoroutineScheduler , EventLoop\n";

    while (_isrunning && !_shouldshutdown)
    {
        std::vector<IOuringOPData *> opdatas;
        if (!GetDoneIOEvents(opdatas))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            break;
        }

        uint32_t otherprocesscount = 0;
        for (int i = 0; i < opdatas.size(); i++)
        {
            auto opdata = opdatas[i];
            if (opdata->OP_Type == IOUring_OPType::OP_SHUTDOWN)
            {
                _shouldshutdown = true;
                std::cout << "IOuringCore , EventLoop closing......\n";
                SAFE_DELETE(opdata);
                continue;
            }
            EventProcess(opdata);
            SAFE_DELETE(opdata);
            if (_shouldshutdown)
                break;
        }
        if (_shouldshutdown)
            break;
        opdatas.clear();
    }

    io_uring_queue_exit(&ring);
}

bool CoroutineScheduler::GetDoneIOEvents(std::vector<IOuringOPData *> &opdatas)
{
    // 非阻塞检测
    io_uring_cqe *unusecqe = nullptr;
    int ret = io_uring_wait_cqe(&ring, &unusecqe);
    if (ret < 0 && ret != -EINTR)
    {
        std::cerr << "io_uring_submit_and_wait failed errno:" << errno << std::endl;
        return false;
    }

    while (true)
    {
        unsigned ready = io_uring_cq_ready(&ring);
        if (ready <= 0)
        {
            break;
        }
        else /* if (ready > 0) */
        {
            static unsigned max_batch = 10000;
            unsigned batch_size = std::min(max_batch, ready);

            io_uring_cqe *cqes[batch_size];
            int cqecount = io_uring_peek_batch_cqe(&ring, cqes, batch_size);
            if (cqecount > 0)
            {
                for (int i = 0; i < cqecount; i++)
                {
                    io_uring_cqe *cqe = cqes[i];
                    IOuringOPData *opdata = (IOuringOPData *)io_uring_cqe_get_data(cqe);
                    if (opdata)
                    {
                        if (opdata->res)
                            (*opdata->res) = cqe->res;
                        opdatas.emplace_back(opdata);
                    }
                }
                io_uring_cq_advance(&ring, cqecount);
            }
        }
    }
    return true;
}

int CoroutineScheduler::EventProcess(IOuringOPData *opdata)
{
    if (!opdata)
        return -1;

    if (opdata->OP_Type == IOUring_OPType::OP_TimeOut)
    {
        int timer_fd = opdata->fd;

        std::shared_ptr<CoTimer::Handle> handle;
        if (_timers.Find(timer_fd, handle))
        {
            if (!handle->active)
            {
                _timers.Erase(timer_fd);
                return 1;
            }

            {
                LockGuard lock(handle->corolock);
                if (handle->active)
                {
                    std::coroutine_handle<> coro = handle->coroutine;
                    if (coro && !coro.done())
                    {
                        auto task = [coro, handle]() -> void
                        {
                            if (handle)
                            {
                                bool expected = false;
                                if (handle->corodone.compare_exchange_strong(expected, true))
                                {
                                    if (!coro.done())
                                    {
                                        coro.resume();
                                        handle->coroutine = nullptr;
                                    }
                                }
                            }
                        };
                        auto expected = CoTimer::WakeType::RUNNING;
                        handle->wakeresult.compare_exchange_strong(expected, CoTimer::WakeType::TIMEOUT);
                        ExcuteCoroutine(task); // 恢复对应的协程
                    }
                    handle->active = false;
                }
            }
            _timers.Erase(timer_fd);
        }
    }
    else if (opdata->OP_Type == IOUring_OPType::OP_Coroutine)
    {
        if (opdata->coroutine && !opdata->coroutine.done())
        {
            auto task = [coro = opdata->coroutine]() -> void
            {
                if (!coro.done())
                    coro.resume();
            };
            ExcuteCoroutine(task);
        }
    }
    else if (opdata->OP_Type == IOUring_OPType::OP_Connect)
    {
        if (opdata->coroutine && !opdata->coroutine.done())
        {
            auto task = [coro = opdata->coroutine]() -> void
            {
                if (!coro.done())
                    coro.resume();
            };
            ExcuteCoroutine(task);
        }
    }
    return 1;
}

void CoroutineScheduler::DoPostIOEvents(std::vector<IOuringOPData *> &opdatas)
{
    std::lock_guard<CriticalSectionLock> lock(_doPostIOEventLock);

    for (auto opdata : opdatas)
    {
        switch (opdata->OP_Type)
        {
        case IOUring_OPType::OP_TimeOut:
        {
            SubmitTimerEvent(opdata);
        }
        break;
        case IOUring_OPType::OP_Coroutine:
        {
            SubmitCoroutineEvent(opdata);
        }
        break;
        case IOUring_OPType::OP_Connect:
        {
            SubmitConnectEvent(opdata);
        }
        break;
        default:
            SAFE_DELETE(opdata);
            break;
        }
    }
    int submitted = io_uring_submit(&ring);
    if (submitted < 0)
    {
        std::cout << "io_uring_submit error!";
    }
}

CoroutineScheduler *CoroutineScheduler::Instance()
{
    static CoroutineScheduler *m_instance = new CoroutineScheduler();
    return m_instance;
}

std::shared_ptr<CoTimer::Handle> CoroutineScheduler::create_timer(std::chrono::milliseconds interval)
{
    int timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (timer_fd < 0)
    {
        return std::shared_ptr<CoTimer::Handle>(nullptr);
    }

    struct itimerspec timerspec;
    timerspec.it_value.tv_sec = interval.count() / 1000;
    timerspec.it_value.tv_nsec = (interval.count() % 1000) * 1000000;
    timerspec.it_interval.tv_sec = 0;
    timerspec.it_interval.tv_nsec = 0;

    if (timerfd_settime(timer_fd, 0, &timerspec, nullptr) < 0)
    {
        close(timer_fd);
        return std::shared_ptr<CoTimer::Handle>(nullptr);
    }

    auto handle = std::make_shared<CoTimer::Handle>();
    handle->timer_fd = timer_fd;
    handle->active = true;
    handle->wakeresult = CoTimer::WakeType::RUNNING;

    if (!_timers.Insert(timer_fd, handle))
        return std::shared_ptr<CoTimer::Handle>(nullptr);

    IOuringOPData *opdata = new IOuringOPData(IOUring_OPType::OP_TimeOut, timer_fd);
    // 提交读取定时器的SQE
    _optaskqueue.enqueue(opdata);
    _IOEventCV.notify_one();

    return handle;
}

// 立即唤醒定时器
void CoroutineScheduler::wake_timer(std::shared_ptr<CoTimer::Handle> handle)
{
    if (!handle || !handle->active)
        return;

    auto expected = CoTimer::WakeType::RUNNING;
    handle->wakeresult.compare_exchange_strong(expected, CoTimer::WakeType::MANUAL_WAKE);

    // 设置定时器立即触发
    struct itimerspec new_value;
    new_value.it_value.tv_sec = 0;
    new_value.it_value.tv_nsec = 1; // 1纳秒后触发
    new_value.it_interval.tv_sec = 0;
    new_value.it_interval.tv_nsec = 0;

    timerfd_settime(handle->timer_fd, 0, &new_value, nullptr);
}

// 注册协程到定时器
bool CoroutineScheduler::RegisterTimerCoroutine(std::shared_ptr<CoTimer::Handle> handle, std::coroutine_handle<> coroutine)
{
    if (!handle || !handle->active || coroutine.done())
        return false;

    std::shared_ptr<CoTimer::Handle> temp;
    if (!_timers.Find(handle->timer_fd, temp) || temp != handle)
        return false;

    LockGuard lock(handle->corolock);
    if (!handle->active || coroutine.done())
        return false;
    handle->coroutine = coroutine;
    return true;
}

void CoroutineScheduler::RegisterTaskCoroutine(std::coroutine_handle<> coroutine)
{
    if (!coroutine || coroutine.done())
        return;

    IOuringOPData *opdata = new IOuringOPData(IOUring_OPType::OP_Coroutine, coroutine);
    _optaskqueue.enqueue(opdata);
    _IOEventCV.notify_one();
}

void CoroutineScheduler::RegisterConnectCoroutine(int fd, sockaddr_in addr, int *res, std::coroutine_handle<> coroutine)
{
    if (!coroutine || coroutine.done())
        return;

    IOuringOPData *opdata = new IOuringOPData(IOUring_OPType::OP_Connect, coroutine);
    opdata->fd = fd;
    opdata->addr = addr;
    opdata->res = res;
    _optaskqueue.enqueue(opdata);
    _IOEventCV.notify_one();
}

void CoroutineScheduler::UnBindTaskCoroutineThread(std::coroutine_handle<> coroutine)
{
    if (!coroutine)
        return;

    // LockGuard lock(_coroutine_thread_map_lock);
    // auto it = _coroutine_thread_map.find(coroutine);
    // if (it != _coroutine_thread_map.end())
    //     _coroutine_thread_map.erase(it);
}

bool CoroutineScheduler::SubmitTimerEvent(IOuringOPData *opdata)
{
    io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    if (!sqe)
        return false;

    io_uring_prep_poll_add(sqe, opdata->fd, POLL_IN);
    io_uring_sqe_set_data(sqe, (void *)opdata);

    return true;
}

bool CoroutineScheduler::SubmitCoroutineEvent(IOuringOPData *opdata)
{
    io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    if (!sqe)
        return false;

    io_uring_prep_nop(sqe);
    io_uring_sqe_set_data(sqe, (void *)opdata);

    return true;
}

bool CoroutineScheduler::SubmitConnectEvent(IOuringOPData *opdata)
{
    io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    if (!sqe)
        return false;

    io_uring_prep_connect(sqe, opdata->fd, (struct sockaddr *)&opdata->addr, sizeof(struct sockaddr));
    io_uring_sqe_set_data(sqe, (void *)opdata);

    return true;
}

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

void UnBindCoroutineThreadFromScheduler(std::coroutine_handle<> coroutine)
{
    CoroutineScheduler::Instance()->UnBindTaskCoroutineThread(coroutine);
}

CoConnection::CoConnection(int fd, sockaddr_in addr)
    : fd(fd), addr(addr) {}

CoConnection::~CoConnection() {}

CoConnection::Awaiter CoConnection::operator co_await()
{
    return CoConnection::Awaiter(fd, addr);
}

CoTimer::Handle::Handle()
    : wakeresult(CoTimer::WakeType::RUNNING), timer_fd(-1), active(false)
{
}

CoTimer::Handle::~Handle()
{
    if (timer_fd > 0)
    {
        close(timer_fd);
        timer_fd = -1;
    }
}
