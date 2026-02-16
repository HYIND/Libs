#include <sys/eventfd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include "Coroutine.h"
#include "string.h"
#include <sys/timerfd.h>
#include <arpa/inet.h>
#include "CoroutineScheduler_Linux.h"

#define SAFE_DELETE(x) \
    if (x)             \
    {                  \
        delete x;      \
        x = nullptr;   \
    }

#define ENTRIES 3000

static int shutdown_eventfd = -1;

class CoroutineScheduler;

enum class Coro_IOUring_OPType
{
    OP_TimeOut = 1,   // 定时器
    OP_Coroutine = 2, // 协程执行
    OP_Connect = 3,   // socket connect
    OP_SHUTDOWN
};
struct Coro_IOuringOPData
{
    Coro_IOUring_OPType OP_Type;
    int fd = 0;
    int res = -1;
    char buffer[sizeof(uint64_t)];

    std::shared_ptr<TaskHandle> task_handle;
    std::shared_ptr<CoConnection::Handle> connection_handle;
    std::shared_ptr<CoTimer::Handle> timer_handle;

    Coro_IOuringOPData(Coro_IOUring_OPType OP_Type, int fd)
        : OP_Type(OP_Type), fd(fd) {}
    Coro_IOuringOPData(Coro_IOUring_OPType OP_Type, std::shared_ptr<TaskHandle> handle)
        : OP_Type(OP_Type), task_handle(handle) {}
    Coro_IOuringOPData(Coro_IOUring_OPType OP_Type, std::shared_ptr<CoConnection::Handle> handle)
        : OP_Type(OP_Type), connection_handle(handle), fd(handle->socket) {}
    Coro_IOuringOPData(Coro_IOUring_OPType OP_Type, std::shared_ptr<CoTimer::Handle> handle)
        : OP_Type(OP_Type), timer_handle(handle) {}
};

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

    if (!Running() && _isinitsuccess)
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

            Coro_IOuringOPData *opdata = new Coro_IOuringOPData(Coro_IOUring_OPType::OP_SHUTDOWN, shutdown_eventfd);
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

bool CoroutineScheduler::AddReadShutDownEvent(Coro_IOuringOPData *opdata)
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

        std::vector<Coro_IOuringOPData *> opdatas;
        constexpr size_t BATCH_SIZE = 100;
        while (!_optaskqueue.empty())
        {
            Coro_IOuringOPData *temp = nullptr;
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
        std::vector<Coro_IOuringOPData *> opdatas;
        if (!GetDoneIOEvents(opdatas))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            break;
        }

        uint32_t otherprocesscount = 0;
        for (int i = 0; i < opdatas.size(); i++)
        {
            auto opdata = opdatas[i];
            if (opdata->OP_Type == Coro_IOUring_OPType::OP_SHUTDOWN)
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

bool CoroutineScheduler::GetDoneIOEvents(std::vector<Coro_IOuringOPData *> &opdatas)
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
                    Coro_IOuringOPData *opdata = (Coro_IOuringOPData *)io_uring_cqe_get_data(cqe);
                    if (opdata)
                    {
                        opdata->res = cqe->res;
                        opdatas.emplace_back(opdata);
                    }
                }
                io_uring_cq_advance(&ring, cqecount);
            }
        }
    }
    return true;
}

int CoroutineScheduler::EventProcess(Coro_IOuringOPData *opdata)
{
    if (!opdata)
        return -1;

    if (opdata->OP_Type == Coro_IOUring_OPType::OP_TimeOut)
    {
        auto handle = opdata->timer_handle;

        if (!handle || !handle->active)
            return 1;

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
                handle->result.compare_exchange_strong(expected, CoTimer::WakeType::TIMEOUT);
                ExcuteCoroutine(task); // 恢复对应的协程
            }
            handle->active = false;
        }
    }
    else if (opdata->OP_Type == Coro_IOUring_OPType::OP_Coroutine)
    {
        auto handle = opdata->task_handle;

        if (handle->coroutine && !handle->coroutine.done())
        {
            auto task = [handle]() -> void
            {
                auto coro = handle->coroutine;
                if (!coro.done())
                    coro.resume();
            };
            ExcuteCoroutine(task);
        }
    }
    else if (opdata->OP_Type == Coro_IOUring_OPType::OP_Connect)
    {
        auto handle = opdata->connection_handle;
        if (!handle || !handle->active)
            return 1;

        if (opdata->res < 0)
        {
            handle->socket = 0;
            CoCloseSocket(handle->socket);
        }
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
                ExcuteCoroutine(task); // 恢复对应的协程
            }
            handle->active = false;
        }
    }
    return 1;
}

void CoroutineScheduler::DoPostIOEvents(std::vector<Coro_IOuringOPData *> &opdatas)
{
    std::lock_guard<CriticalSectionLock> lock(_doPostIOEventLock);

    for (auto opdata : opdatas)
    {
        switch (opdata->OP_Type)
        {
        case Coro_IOUring_OPType::OP_TimeOut:
        {
            SubmitTimeOutEvent(opdata);
        }
        break;
        case Coro_IOUring_OPType::OP_Coroutine:
        {
            SubmitCoroutineEvent(opdata);
        }
        break;
        case Coro_IOUring_OPType::OP_Connect:
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
    auto handle = std::make_shared<CoTimer::Handle>();

    auto timer = TimerTask::CreateOnce("", interval.count(), [handle, this]() -> void {
        Coro_IOuringOPData* opdata = new Coro_IOuringOPData(Coro_IOUring_OPType::OP_TimeOut, handle);
        _optaskqueue.enqueue(opdata);
        _IOEventCV.notify_one(); });

    if (!timer)
    {
        return std::shared_ptr<CoTimer::Handle>(nullptr);
    }

    handle->task = timer;
    handle->task->Run();
    return handle;
}

// 立即唤醒定时器
void CoroutineScheduler::wake_timer(std::shared_ptr<CoTimer::Handle> handle)
{
    if (!handle || !handle->active)
        return;

    auto expected = CoTimer::WakeType::RUNNING;
    handle->result.compare_exchange_strong(expected, CoTimer::WakeType::MANUAL_WAKE);

    if (!handle->task)
        return;
    handle->task->Wake();
}

std::shared_ptr<TaskHandle> CoroutineScheduler::RegisterTaskCoroutine(std::coroutine_handle<> coroutine)
{
    if (!coroutine || coroutine.done())
        return std::shared_ptr<TaskHandle>();

    auto handle = std::make_shared<TaskHandle>(coroutine);

    Coro_IOuringOPData *opdata = new Coro_IOuringOPData(Coro_IOUring_OPType::OP_Coroutine, handle);
    _optaskqueue.enqueue(opdata);
    _IOEventCV.notify_one();

    return handle;
}

std::shared_ptr<CoConnection::Handle> CoroutineScheduler::create_connection(BaseSocket fd, const std::string& IP, int port)
{
    auto handle = std::make_shared<CoConnection::Handle>();
    handle->socket = fd;

    memset(&handle->remoteaddr, 0, sizeof(handle->remoteaddr));
    handle->remoteaddr.sin_family = AF_INET;
    handle->remoteaddr.sin_port = htons(port);
    inet_pton(AF_INET, IP.c_str(), &(handle->remoteaddr.sin_addr.s_addr));

    Coro_IOuringOPData *opdata = new Coro_IOuringOPData(Coro_IOUring_OPType::OP_Connect, handle);
    opdata->fd = fd;
    _optaskqueue.enqueue(opdata);
    _IOEventCV.notify_one();

    return handle;
}

bool CoroutineScheduler::SubmitTimeOutEvent(Coro_IOuringOPData *opdata)
{
    if (!opdata->timer_handle)
        return false;
    io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    if (!sqe)
        return false;

    io_uring_prep_nop(sqe);
    io_uring_sqe_set_data(sqe, (void *)opdata);

    return true;
}

bool CoroutineScheduler::SubmitCoroutineEvent(Coro_IOuringOPData *opdata)
{
    if (!opdata->task_handle)
        return false;
    io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    if (!sqe)
        return false;

    io_uring_prep_nop(sqe);
    io_uring_sqe_set_data(sqe, (void *)opdata);

    return true;
}

bool CoroutineScheduler::SubmitConnectEvent(Coro_IOuringOPData *opdata)
{
    if (!opdata->connection_handle)
        return false;
    io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    if (!sqe)
        return false;

    io_uring_prep_connect(sqe, opdata->connection_handle->socket, (struct sockaddr *)&opdata->connection_handle->remoteaddr, sizeof(struct sockaddr));
    io_uring_sqe_set_data(sqe, (void *)opdata);

    return true;
}
