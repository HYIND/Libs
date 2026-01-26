
#include "Core/NetCoredef.h"
#include "Core/IOuringCore.h"
#include "ResourcePool.h"
#include "ThreadPool.h"

#include "Session/SessionListener.h"
#include "Session/BaseNetWorkSession.h"
#include "EndPoint/TCPEndPoint.h"
#include "EndPoint/TcpEndPointListener.h"

#define ENTRIES 15000

#define RECVBUFFERMINLEN 1024
#define RECVBUFFERDEFLEN 1024 * 5
#define RECVBUFFERMAXLEN 1024 * 1024

#define SENDBUFFERCONCATMAXLEN 1024 * 1024
#define RECVBUFFERCONCATMAXLEN 1024 * 1024

static BaseSocket shutdown_eventfd = -1;

enum class IOUring_OPType
{
    OP_READ = 1,
    OP_WRITE = 2,
    OP_ACCEPT = 3,
    OP_WILLWRITE = 4,
    OP_SHUTDOWN
};
struct IOuringOPData
{
    IOUring_OPType OP_Type;
    BaseSocket fd;
    std::weak_ptr<BaseTransportConnection> weakCon;
    BaseTransportConnection *raw_ptr;

    Buffer buffer;

    socklen_t addr_len;
    sockaddr_in client_addr; // 客户端地址

    int res; // IO操作完成后，通知前写入结果

    IOuringOPData() : IOuringOPData(IOUring_OPType::OP_READ)
    {
    }
    IOuringOPData(IOUring_OPType OP_Type)
        : OP_Type(OP_Type)
    {
        fd = 0;
        raw_ptr = nullptr;
        addr_len = 0;
        memset(&client_addr, '\0', sizeof(sockaddr_in));
        res = 0;
    }
    ~IOuringOPData()
    {
        Reset();
    }
    void Reset()
    {
        OP_Type = IOUring_OPType::OP_READ;
        fd = 0;
        weakCon.reset();
        raw_ptr = nullptr;
        buffer.Release();
        addr_len = 0;
        memset(&client_addr, '\0', sizeof(sockaddr_in));
        res = 0;
    }

    static IOuringOPData *CreateWriteOP(std::shared_ptr<BaseTransportConnection> Con, Buffer &buf);
    static IOuringOPData *CreateReadOP(std::shared_ptr<BaseTransportConnection> Con, uint32_t size);
    static IOuringOPData *CreateAcceptOP(std::shared_ptr<BaseTransportConnection> Con);
    static IOuringOPData *CreateWillWriteOP(std::shared_ptr<BaseTransportConnection> Con);
};

class IOuringCoreProcessImpl
{

    class SequentialIOSubmitter;
    class SequentialEventExecutor;
    class DynamicBufferState;

private:
    struct NetCore_IOuringData
    {
        BaseSocket fd;
        std::weak_ptr<BaseTransportConnection> weakCon;
        std::vector<std::shared_ptr<SequentialIOSubmitter>> senders; // IO流水线
        std::shared_ptr<SequentialEventExecutor> recver;             // 事件处理流水线（包含ACCEPT）
        std::shared_ptr<DynamicBufferState> state;

        bool SubmitIOEvent(IOuringOPData *opdata);
        void GetPostIOEvent(std::vector<IOuringOPData *> &out);
        void NotifyIOEventDone(IOuringOPData *opdata);
        void NotifyIOEventRetry(IOuringOPData *opdata);
    };

    class SequentialIOSubmitter : public std::enable_shared_from_this<SequentialIOSubmitter>
    {
        enum class submitstate
        {
            none = -1,
            idle = 0,
            doing = 1
        };

    public:
        SequentialIOSubmitter(IOuringCoreProcessImpl *core, std::shared_ptr<NetCore_IOuringData> &data, IOUring_OPType type);
        ~SequentialIOSubmitter();
        void Release();
        void SubmitOPdata(IOuringOPData *opdata);
        void NotifyRetry(IOuringOPData *opdata);
        void NotifyDone(IOuringOPData *opdata);
        IOUring_OPType GetSubmitType();
        void GetPostIOEvent(std::vector<IOuringOPData *> &out);

    private:
        IOuringCoreProcessImpl *_core;
        std::weak_ptr<NetCore_IOuringData> _weakdata;
        IOUring_OPType _type;

        CriticalSectionLock _lock;
        SafeDeQue<IOuringOPData *> _queue;
        submitstate _state;
    };

    class SequentialEventExecutor
    {
        enum class excutestate;

    public:
        struct ExcuteEvent;

    public:
        SequentialEventExecutor(IOuringCoreProcessImpl *_core, std::shared_ptr<NetCore_IOuringData> &data);
        ~SequentialEventExecutor();
        void Release();
        void SubmitExcuteEvent(std::shared_ptr<ExcuteEvent> event);
        void NotifyDone();
        void GetPostExcuteEvent(std::vector<std::shared_ptr<ExcuteEvent>> &out);
        bool Active();
        void SetActive(bool value);

    private:
        IOuringCoreProcessImpl *_core;
        std::weak_ptr<NetCore_IOuringData> _weakdata;
        CriticalSectionLock _lock;
        SafeQueue<std::shared_ptr<ExcuteEvent>> _queue;
        excutestate _state;
        bool _active;
    };

    // 动态缓冲区管理,用于动态调整连接的缓冲区以适应突发的流量
    class DynamicBufferState
    {
    public:
        DynamicBufferState();
        void Update(uint32_t newbufferrecvlen);
        uint32_t GetDynamicSize();

    private:
        uint32_t lastbuffersize;
    };

public:
    IOuringCoreProcessImpl();
    int Run();
    void Stop();
    bool Running();

public:
    bool AddNetFd(std::shared_ptr<BaseTransportConnection> Con);
    bool DelNetFd(BaseTransportConnection *Con);
    bool SendRes(std::shared_ptr<BaseTransportConnection> BaseCon);
    void AddPendingDeletion(DeleteLaterImpl *ptr);

private:
    void LoopSubmitIOEvent();
    void LoopSubmitExcuteEvent();
    void Loop();
    bool GetDoneIOEvents(std::vector<IOuringOPData *> &opdatas);
    int EventProcess(IOuringOPData *opdata, std::vector<IOuringOPData *> &postOps);
    void ProcessPendingDeletions();
    bool AddReadShutDownEvent(IOuringOPData *opdata);

private:
    void DoPostIOEvents(std::vector<IOuringOPData *> opdatas);
    void DoPostExcuteEvents(std::vector<std::shared_ptr<SequentialEventExecutor::ExcuteEvent>> &events);

    bool SubmitWriteEvent(IOuringOPData *opdata);
    bool SubmitReadEvent(IOuringOPData *opdata);
    bool SubmitAcceptEvent(IOuringOPData *opdata);
    bool SubmitWillWriteEvent(IOuringOPData *opdata);

private:
    bool _shouldshutdown;
    bool _isrunning;
    bool _isinitsuccess;
    io_uring ring;

    SafeMap<BaseTransportConnection *, std::shared_ptr<NetCore_IOuringData>> _IOUringData;
    SafeArray<DeleteLaterImpl *> _pendingDeletions;
    ThreadPool _ExcuteEventProcessPool;

    CriticalSectionLock _IOEventLock;
    ConditionVariable _IOEventCV;

    CriticalSectionLock _ExcuteLock;
    ConditionVariable _ExcuteCV;

    CriticalSectionLock _doPostIOEventLock;
};

int64_t GetTimestampMilliseconds()
{
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

static void setnonblocking(BaseSocket fd)
{
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
}

enum class IOuringCoreProcessImpl::SequentialEventExecutor::excutestate
{
    none = -1,
    idle = 0,
    doing = 1
};
struct IOuringCoreProcessImpl::SequentialEventExecutor::ExcuteEvent
{
    enum class EventType
    {
        READ_DATA,
        ACCEPT_CONNECTION,
        READ_HUP
    };
    EventType type;
    std::weak_ptr<NetCore_IOuringData> weakdata;
    std::shared_ptr<Buffer> data;
    struct
    {
        BaseSocket client_fd;
        sockaddr_in client_addr;
    } accept_info;

    ExcuteEvent(EventType type, std::shared_ptr<NetCore_IOuringData> iodata) : type(type), weakdata(iodata) {}
    ~ExcuteEvent()
    {
        if (data)
            data->Release();
    }

    static std::shared_ptr<ExcuteEvent> CreateReadEvent(std::shared_ptr<NetCore_IOuringData> iodata, Buffer &buf);
    static std::shared_ptr<ExcuteEvent> CreateAcceptEvent(std::shared_ptr<NetCore_IOuringData> iodata, BaseSocket client_fd, sockaddr_in client_addr);
    static std::shared_ptr<ExcuteEvent> CreateRDHUP(std::shared_ptr<NetCore_IOuringData> iodata);
};

class IODataManager : public ResPool<IOuringOPData>
{
public:
    static IODataManager *Instance()
    {
        static IODataManager *Instance = new IODataManager();
        return Instance;
    }
    IODataManager() : ResPool(200, 1000)
    {
    }
    void ResetData(IOuringOPData *data)
    {
        data->Reset();
    }
    IOuringOPData *AllocateData(IOUring_OPType OP_Type, BaseSocket fd, std::shared_ptr<BaseTransportConnection> Con, uint32_t buffersize = RECVBUFFERDEFLEN)
    {
        IOuringOPData *data = ResPool<IOuringOPData>::AllocateData();
        data->OP_Type = OP_Type;
        data->fd = fd;
        data->weakCon = Con;
        data->raw_ptr = Con.get();
        if (OP_Type == IOUring_OPType::OP_READ || OP_Type == IOUring_OPType::OP_WRITE || OP_Type == IOUring_OPType::OP_SHUTDOWN)
            data->buffer.ReSize(buffersize);
        else if (OP_Type == IOUring_OPType::OP_ACCEPT)
        {
            data->addr_len = 0;
            memset(&data->client_addr, 0, sizeof(sockaddr_in));
        }
        return data;
    }
};
#define IODATAMANAGER IODataManager::Instance()

IOuringOPData *IOuringOPData::CreateWriteOP(std::shared_ptr<BaseTransportConnection> Con, Buffer &buf)
{
    IOuringOPData *data = IODATAMANAGER->AllocateData(IOUring_OPType::OP_WRITE, Con->GetFd(), Con, 0);
    int oripos = buf.Position();
    data->buffer.QuoteFromBuf(buf);
    data->buffer.Seek(oripos);
    return data;
}
IOuringOPData *IOuringOPData::CreateReadOP(std::shared_ptr<BaseTransportConnection> Con, uint32_t size)
{
    IOuringOPData *data = IODATAMANAGER->AllocateData(IOUring_OPType::OP_READ, Con->GetFd(), Con, size);
    return data;
}

IOuringOPData *IOuringOPData::CreateAcceptOP(std::shared_ptr<BaseTransportConnection> Con)
{
    IOuringOPData *data = IODATAMANAGER->AllocateData(IOUring_OPType::OP_ACCEPT, Con->GetFd(), Con, 0);
    data->addr_len = sizeof(sockaddr_in);
    return data;
}
IOuringOPData *IOuringOPData::CreateWillWriteOP(std::shared_ptr<BaseTransportConnection> Con)
{
    IOuringOPData *data = IODATAMANAGER->AllocateData(IOUring_OPType::OP_WILLWRITE, Con->GetFd(), Con, 0);
    return data;
}

bool IOuringCoreProcessImpl::NetCore_IOuringData::SubmitIOEvent(IOuringOPData *opdata)
{
    if (!opdata)
        return false;
    for (auto sender : senders)
    {
        if (sender->GetSubmitType() == opdata->OP_Type)
        {
            sender->SubmitOPdata(opdata);
            return true;
        }
    }
    return false;
}

void IOuringCoreProcessImpl::NetCore_IOuringData::GetPostIOEvent(std::vector<IOuringOPData *> &out)
{
    for (auto sender : senders)
    {
        if (sender)
            sender->GetPostIOEvent(out);
    }
}

void IOuringCoreProcessImpl::NetCore_IOuringData::NotifyIOEventDone(IOuringOPData *opdata)
{
    for (auto &sender : senders)
    {
        if (sender->GetSubmitType() == opdata->OP_Type)
        {
            sender->NotifyDone(opdata);
            return;
        }
    }
}
void IOuringCoreProcessImpl::NetCore_IOuringData::NotifyIOEventRetry(IOuringOPData *opdata)
{
    for (auto &sender : senders)
    {
        if (sender->GetSubmitType() == opdata->OP_Type)
        {
            sender->NotifyRetry(opdata);
            return;
        }
    }
}

IOuringCoreProcessImpl::SequentialIOSubmitter::SequentialIOSubmitter(
    IOuringCoreProcessImpl *core,
    std::shared_ptr<NetCore_IOuringData> &data,
    IOUring_OPType type)
    : _core(core), _weakdata(data), _type(type), _state(submitstate::idle)
{
}

IOuringCoreProcessImpl::SequentialIOSubmitter::~SequentialIOSubmitter()
{
    Release();
}

void IOuringCoreProcessImpl::SequentialIOSubmitter::Release()
{
    _state = submitstate::none;

    std::lock_guard<CriticalSectionLock> lock(_lock);
    while (!_queue.empty())
    {
        IOuringOPData *opdata = nullptr;
        if (_queue.dequeue_front(opdata) && opdata)
            IODATAMANAGER->ReleaseData(opdata);
    }
}

void IOuringCoreProcessImpl::SequentialIOSubmitter::SubmitOPdata(IOuringOPData *opdata)
{
    if (!_core)
        return;

    if (!opdata || opdata->OP_Type != _type)
        return;

    if (_state == submitstate::none)
        return;

    std::lock_guard<CriticalSectionLock> lock(_lock);
    if (_state == submitstate::none || !_weakdata.lock())
        return;

    _queue.enqueue_back(opdata);

    if (_state == submitstate::idle)
    {
        _core->_IOEventCV.NotifyOne();
    }
}

void IOuringCoreProcessImpl::SequentialIOSubmitter::NotifyDone(IOuringOPData *opdata)
{
    if (_state == submitstate::none)
        return;

    if (opdata->OP_Type != _type)
        return;

    std::lock_guard<CriticalSectionLock> lock(_lock);
    if (_state == submitstate::none)
        return;

    if (_type == IOUring_OPType::OP_WRITE)
    {
        if (opdata->buffer.Remain())
        {
            // 上次传输未完全完成，创建新的IOuringOPData任务，然后放到队首，继续传输
            if (auto iodata = _weakdata.lock())
            {
                if (auto Con = iodata->weakCon.lock())
                {
                    auto submitopdata = IOuringOPData::CreateWriteOP(Con, opdata->buffer);
                    _queue.enqueue_front(submitopdata);
                }
            }
        }
    }
    _state = submitstate::idle;
    if (!_queue.empty())
        _core->_IOEventCV.NotifyOne();
}

void IOuringCoreProcessImpl::SequentialIOSubmitter::NotifyRetry(IOuringOPData *opdata)
{
    std::lock_guard<CriticalSectionLock> lock(_lock);

    if (auto iodata = _weakdata.lock())
    {
        if (auto Con = iodata->weakCon.lock())
        {
            switch (opdata->OP_Type)
            {
            case IOUring_OPType::OP_WRITE:
            {
                auto submitopdata = IOuringOPData::CreateWriteOP(Con, opdata->buffer);
                _queue.enqueue_front(submitopdata);
            }
            break;
            case IOUring_OPType::OP_READ:
            {
                auto submitopdata = IOuringOPData::CreateReadOP(Con, opdata->buffer.Length());
                _queue.enqueue_front(submitopdata);
            }
            break;
            case IOUring_OPType::OP_ACCEPT:
            {
                auto submitopdata = IOuringOPData::CreateAcceptOP(Con);
                _queue.enqueue_front(submitopdata);
            }
            break;
            case IOUring_OPType::OP_WILLWRITE:
            {
                auto submitopdata = IOuringOPData::CreateWillWriteOP(Con);
                _queue.enqueue_front(submitopdata);
            }
            default:
                break;
            }
        }
    }

    _state = submitstate::idle;
    if (!_queue.empty())
        _core->_IOEventCV.NotifyOne();
}

IOUring_OPType IOuringCoreProcessImpl::SequentialIOSubmitter::GetSubmitType()
{
    return _type;
}

void IOuringCoreProcessImpl::SequentialIOSubmitter::GetPostIOEvent(std::vector<IOuringOPData *> &out)
{
    if (_state != submitstate::idle)
        return;

    std::lock_guard<CriticalSectionLock> lock(_lock);
    if (_state != submitstate::idle)
        return;

    if (!_queue.empty())
    {
        if (_type != IOUring_OPType::OP_WRITE)
        {
            IOuringOPData *opdata;
            if (_queue.dequeue_front(opdata) && opdata)
            {
                out.emplace_back(opdata);
                _state = submitstate::doing;
            }
        }
        else // 尝试合并写事件
        {
            IOuringOPData *fistdata;
            if (_queue.dequeue_front(fistdata) && fistdata)
            {
                // 最多合并9个
                int count = 9;
                while (!_queue.empty() && count > 0)
                {
                    IOuringOPData *opdata = nullptr;
                    if (_queue.front(opdata) &&
                        opdata &&
                        fistdata->buffer.Length() + opdata->buffer.Length() < SENDBUFFERCONCATMAXLEN)
                    {
                        fistdata->buffer.Append(opdata->buffer);
                        _queue.dequeue_front(opdata);
                        IODATAMANAGER->ReleaseData(opdata);
                        count--;
                    }
                    else
                    {
                        break;
                    }
                }
            }

            out.emplace_back(fistdata);
            _state = submitstate::doing;
        }
    }
}

std::shared_ptr<IOuringCoreProcessImpl::SequentialEventExecutor::ExcuteEvent>
IOuringCoreProcessImpl::SequentialEventExecutor::ExcuteEvent::CreateReadEvent(std::shared_ptr<NetCore_IOuringData> iodata, Buffer &buf)
{
    std::shared_ptr<ExcuteEvent> event = std::make_shared<ExcuteEvent>(EventType::READ_DATA, iodata);
    event->data = std::make_shared<Buffer>();
    event->data->QuoteFromBuf(buf);
    return event;
}

std::shared_ptr<IOuringCoreProcessImpl::SequentialEventExecutor::ExcuteEvent>
IOuringCoreProcessImpl::SequentialEventExecutor::ExcuteEvent::CreateAcceptEvent(std::shared_ptr<NetCore_IOuringData> iodata, BaseSocket client_fd, sockaddr_in client_addr)
{
    std::shared_ptr<ExcuteEvent> event = std::make_shared<ExcuteEvent>(EventType::ACCEPT_CONNECTION, iodata);
    event->accept_info.client_fd = client_fd;
    event->accept_info.client_addr = client_addr;
    return event;
}

std::shared_ptr<IOuringCoreProcessImpl::SequentialEventExecutor::ExcuteEvent>
IOuringCoreProcessImpl::SequentialEventExecutor::ExcuteEvent::CreateRDHUP(std::shared_ptr<NetCore_IOuringData> iodata)
{
    std::shared_ptr<ExcuteEvent> event = std::make_shared<ExcuteEvent>(EventType::READ_HUP, iodata);
    return event;
}

IOuringCoreProcessImpl::SequentialEventExecutor::SequentialEventExecutor(
    IOuringCoreProcessImpl *core,
    std::shared_ptr<NetCore_IOuringData> &data)
    : _core(core), _weakdata(data), _state(excutestate::idle), _active(true)
{
}

IOuringCoreProcessImpl::SequentialEventExecutor::~SequentialEventExecutor()
{
    Release();
}

void IOuringCoreProcessImpl::SequentialEventExecutor::Release()
{
    _state = excutestate::none;
    std::lock_guard<CriticalSectionLock> lock(_lock);
    _queue.clear();
}

void IOuringCoreProcessImpl::SequentialEventExecutor::SubmitExcuteEvent(std::shared_ptr<ExcuteEvent> event)
{
    if (!_core)
        return;
    if (_state == excutestate::none)
        return;

    std::lock_guard<CriticalSectionLock> lock(_lock);
    if (_state == excutestate::none || !_weakdata.lock())
        return;

    _queue.enqueue(event);

    if (_state == excutestate::idle)
    {
        _core->_ExcuteCV.NotifyOne();
    }
}

void IOuringCoreProcessImpl::SequentialEventExecutor::NotifyDone()
{
    if (_state == excutestate::none)
        return;

    std::lock_guard<CriticalSectionLock> lock(_lock);
    if (_state == excutestate::none)
        return;

    _state = excutestate::idle;
    if (!_queue.empty())
        _core->_ExcuteCV.NotifyOne();
}

void IOuringCoreProcessImpl::SequentialEventExecutor::GetPostExcuteEvent(std::vector<std::shared_ptr<ExcuteEvent>> &out)
{
    if (_state != excutestate::idle)
        return;

    std::lock_guard<CriticalSectionLock> lock(_lock);
    if (_state != excutestate::idle)
        return;

    if (_queue.empty())
        return;

    std::shared_ptr<ExcuteEvent> firstevent;
    if (_queue.dequeue(firstevent) && firstevent)
    {
        if (firstevent->type == ExcuteEvent::EventType::READ_DATA) // 尝试合并后续的读事件
        {
            int count = 9;
            while (!_queue.empty() && count > 0)
            {
                std::shared_ptr<ExcuteEvent> event;
                if (_queue.front(event) &&
                    event &&
                    event->type == firstevent->type &&
                    event->data &&
                    firstevent->data->Length() + event->data->Length() < RECVBUFFERCONCATMAXLEN)
                {
                    firstevent->data->Append(*(event->data));
                    _queue.dequeue(event);
                    count--;
                }
                else
                {
                    break;
                }
            }
        }

        out.emplace_back(firstevent);
        _state = excutestate::doing;
    }
}

bool IOuringCoreProcessImpl::SequentialEventExecutor::Active()
{
    return _active;
}

void IOuringCoreProcessImpl::SequentialEventExecutor::SetActive(bool value)
{
    _active = value;
}

IOuringCoreProcessImpl::DynamicBufferState::DynamicBufferState()
{
    lastbuffersize = RECVBUFFERDEFLEN;
}

void IOuringCoreProcessImpl::DynamicBufferState::Update(uint32_t newbufferrecvlen)
{
    if (newbufferrecvlen >= lastbuffersize)
    {
        if (lastbuffersize < RECVBUFFERMAXLEN)
            lastbuffersize = std::min((uint32_t)RECVBUFFERMAXLEN, (uint32_t)((float)lastbuffersize * 1.5f));
    }
    else
    {
        if (lastbuffersize > RECVBUFFERMINLEN)
            lastbuffersize = std::max((uint32_t)RECVBUFFERMINLEN, std::max(newbufferrecvlen, (uint32_t)((float)lastbuffersize * 0.67f)));
    }
}

uint32_t IOuringCoreProcessImpl::DynamicBufferState::GetDynamicSize()
{
    return lastbuffersize;
}

IOuringCoreProcessImpl::IOuringCoreProcessImpl()
    : _shouldshutdown(false), _isinitsuccess(false), _isrunning(false), _ExcuteEventProcessPool(4)
{
    memset(&ring, 0, sizeof(ring));
    int ret = io_uring_queue_init(ENTRIES, &ring, 0);
    if (ret < 0)
    {
        _isinitsuccess = false;
        perror("io_uring_queue_init fail!");
    }
    else 
        _isinitsuccess = true;

    if (shutdown_eventfd < 0)
    {
        shutdown_eventfd = eventfd(0, EFD_NONBLOCK);
        if (shutdown_eventfd < 0)
        {
            perror("shutdown_eventfd init error!");
        }
    }
}

int IOuringCoreProcessImpl::Run()
{
    try
    {
        _isrunning = true;
        _shouldshutdown = false;
        if (!_isinitsuccess)
        {
            memset(&ring, 0, sizeof(ring));
            int ret = io_uring_queue_init(ENTRIES, &ring, 0);
            if (ret < 0)
            {
                perror("io_uring_queue_init fail!");
                return -1;
            }
            else
                _isinitsuccess = true;
        }

        if (shutdown_eventfd > 0)
        {
            uint64_t value = 0;
            ssize_t n = read(shutdown_eventfd, &value, sizeof(value));

            IOuringOPData *opdata = IODATAMANAGER->AllocateData(IOUring_OPType::OP_SHUTDOWN, shutdown_eventfd, nullptr, sizeof(uint64_t));
            opdata->fd = shutdown_eventfd;
            AddReadShutDownEvent(opdata);
        }
        _ExcuteEventProcessPool.start();
        std::thread IOEventLoop(&IOuringCoreProcessImpl::LoopSubmitIOEvent, this);
        std::thread ExcuteEventLoop(&IOuringCoreProcessImpl::LoopSubmitExcuteEvent, this);
        std::thread EventLoop(&IOuringCoreProcessImpl::Loop, this);
        EventLoop.join();
        IOEventLoop.join();
        ExcuteEventLoop.join();
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

bool IOuringCoreProcessImpl::Running()
{
    return _isrunning;
}

void IOuringCoreProcessImpl::Stop()
{
    if (shutdown_eventfd < 0)
        return;
    uint64_t num = 1;
    write(shutdown_eventfd, &num, sizeof(uint64_t));
}

bool IOuringCoreProcessImpl::AddReadShutDownEvent(IOuringOPData *opdata)
{
    io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    if (!sqe)
        return false;

    io_uring_prep_read(sqe, opdata->fd, opdata->buffer.Data(), opdata->buffer.Length(), 0);
    io_uring_sqe_set_data(sqe, (void *)opdata);

    int submitted = io_uring_submit(&ring);
    if (submitted < 0)
    {
        perror("shutdown_eventfd submit to io_uring error!");
        return false;
    }

    return true;
}

bool IOuringCoreProcessImpl::AddNetFd(std::shared_ptr<BaseTransportConnection> Con)
{
    // cout << "AddNetFd fd :" << Con->GetFd() << endl;
    if (!Con || Con->GetFd() <= 0)
        return false;

    std::shared_ptr<NetCore_IOuringData> iodata = std::make_shared<NetCore_IOuringData>();
    std::weak_ptr<BaseTransportConnection> weak(Con);
    iodata->fd = Con->GetFd();
    iodata->weakCon = weak;
    iodata->recver = std::make_shared<SequentialEventExecutor>(this, iodata);
    iodata->state = std::make_shared<DynamicBufferState>();

    if (Con->GetNetType() == NetType::Client)
    {
        iodata->senders.emplace_back(std::make_shared<SequentialIOSubmitter>(this, iodata, IOUring_OPType::OP_WRITE));
        iodata->senders.emplace_back(std::make_shared<SequentialIOSubmitter>(this, iodata, IOUring_OPType::OP_READ));
        iodata->senders.emplace_back(std::make_shared<SequentialIOSubmitter>(this, iodata, IOUring_OPType::OP_WILLWRITE));
        _IOUringData.Insert(Con.get(), iodata);

        // setnonblocking(Con->GetFd());
        IOuringOPData *opdata = IOuringOPData::CreateReadOP(Con, iodata->state->GetDynamicSize());
        iodata->SubmitIOEvent(opdata);
    }
    else if (Con->GetNetType() == NetType::Listener)
    {
        iodata->senders.emplace_back(std::make_shared<SequentialIOSubmitter>(this, iodata, IOUring_OPType::OP_ACCEPT));
        _IOUringData.Insert(Con.get(), iodata);

        IOuringOPData *opdata = IOuringOPData::CreateAcceptOP(Con);
        iodata->SubmitIOEvent(opdata);
    }

    return true;
}

bool IOuringCoreProcessImpl::DelNetFd(BaseTransportConnection *Con)
{
    _IOUringData.Erase(Con);
    return true;
}

bool IOuringCoreProcessImpl::SendRes(std::shared_ptr<BaseTransportConnection> BaseCon)
{
    if (!BaseCon || !(BaseCon->GetNetType() == NetType::Client))
        return false;

    std::shared_ptr<TCPTransportConnection> Con = BaseCon->GetShared<TCPTransportConnection>();
    if (!Con)
        return false;

    LockGuard lock(Con->GetSendMtx(), true);
    if (!lock.isownlock())
        return true; // 写锁正在被其他线程占用

    BaseSocket fd = Con->GetSocket();
    SafeQueue<Buffer *> &SendDatas = Con->GetSendData();

    std::shared_ptr<NetCore_IOuringData> iodata;
    if (!_IOUringData.Find(Con.get(), iodata))
    {
        iodata = std::make_shared<NetCore_IOuringData>();
        auto weak = std::weak_ptr<BaseTransportConnection>(BaseCon);
        iodata->fd = Con->GetSocket();
        iodata->weakCon = weak;
        iodata->senders.emplace_back(std::make_shared<SequentialIOSubmitter>(this, iodata, IOUring_OPType::OP_WRITE));
        iodata->senders.emplace_back(std::make_shared<SequentialIOSubmitter>(this, iodata, IOUring_OPType::OP_READ));
        iodata->senders.emplace_back(std::make_shared<SequentialIOSubmitter>(this, iodata, IOUring_OPType::OP_WILLWRITE));
        iodata->recver = std::make_shared<SequentialEventExecutor>(this, iodata);
        iodata->state = std::make_shared<DynamicBufferState>();
        if (!_IOUringData.Insert(Con.get(), iodata))
        {
            if (!_IOUringData.Find(Con.get(), iodata))
                return false;
        }
    }

    int count = 0;
    while (count < 10 && !SendDatas.empty())
    {

        Buffer *buffer = nullptr;
        if (!SendDatas.dequeue(buffer))
            break;

        if (buffer)
        {
            if (!buffer->Data() || buffer->Length() <= 0)
            {
                // 无效Buffer不发送
                SAFE_DELETE(buffer);
            }
            else
            {

                IOuringOPData *opdata = IOuringOPData::CreateWriteOP(Con, *buffer);
                bool submit = iodata->SubmitIOEvent(opdata);
                SAFE_DELETE(buffer);
                if (!submit)
                    break;
            }
        }
        count++;
    }

    if (!SendDatas.empty()) // 仍有数据未发送,关注其可写事件,等待下次可写事件
    {
        IOuringOPData *opdata = IOuringOPData::CreateWillWriteOP(Con);
        if (!iodata->SubmitIOEvent(opdata))
            return false;
    }

    return true;
}

void IOuringCoreProcessImpl::LoopSubmitIOEvent()
{

    if (!_isrunning || _shouldshutdown)
        return;

    do
    {
        LockGuard guard(_IOEventLock);
        _IOEventCV.WaitFor(guard, std::chrono::milliseconds(50));

        if (_shouldshutdown || !_isrunning)
            break;

        std::vector<IOuringOPData *> opdatas;
        auto GetEvents = [&opdatas](std::map<BaseTransportConnection *, std::shared_ptr<IOuringCoreProcessImpl::NetCore_IOuringData>> &map) -> void
        {
            for (const auto &[Con, iodata] : map)
            {
                for (size_t i = 0; i < iodata->senders.size(); ++i)
                {
                    iodata->senders[i]->GetPostIOEvent(opdatas);
                }
            }
        };
        _IOUringData.EnsureCall(GetEvents);
        if (opdatas.size() > 0)
            DoPostIOEvents(opdatas);
    } while ((_isrunning && !_shouldshutdown));
}

void IOuringCoreProcessImpl::LoopSubmitExcuteEvent()
{
    if (!_isrunning || _shouldshutdown)
        return;

    do
    {
        LockGuard guard(_ExcuteLock);
        _ExcuteCV.WaitFor(guard, std::chrono::milliseconds(50));

        if (_shouldshutdown || !_isrunning)
            break;

        std::vector<std::shared_ptr<SequentialEventExecutor::ExcuteEvent>> events;
        auto GetEvents = [&events](std::map<BaseTransportConnection *, std::shared_ptr<IOuringCoreProcessImpl::NetCore_IOuringData>> &map) -> void
        {
            for (auto it = map.begin(); it != map.end(); it++)
            {
                auto &iodata = it->second;
                iodata->recver->GetPostExcuteEvent(events);
            }
        };
        _IOUringData.EnsureCall(GetEvents);
        if (events.size() > 0)
            DoPostExcuteEvents(events);
    } while (_isrunning && !_shouldshutdown);
}

void IOuringCoreProcessImpl::Loop()
{
    std::cout << "IOuringCore , EventLoop\n";

    while (_isrunning && !_shouldshutdown)
    {
        std::vector<IOuringOPData *> opdatas;
        if (!GetDoneIOEvents(opdatas))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            break;
        }

        std::vector<IOuringOPData *> postOps;
        uint32_t otherprocesscount = 0;
        for (int i = 0; i < opdatas.size(); i++)
        {
            auto opdata = opdatas[i];
            if (opdata->OP_Type == IOUring_OPType::OP_SHUTDOWN)
            {
                _shouldshutdown = true;
                std::cout << "IOuringCore , EventLoop closing......\n";
                IODATAMANAGER->ReleaseData(opdata);
                continue;
            }
            EventProcess(opdata, postOps);
            IODATAMANAGER->ReleaseData(opdata);
        }
        opdatas.clear();

        ProcessPendingDeletions();

        if (_shouldshutdown)
            break;

        if (postOps.size() > 0)
        {
            DoPostIOEvents(postOps);
            postOps.clear();
        }
    }

    io_uring_queue_exit(&ring);
}

bool IOuringCoreProcessImpl::GetDoneIOEvents(std::vector<IOuringOPData *> &opdatas)
{
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

int IOuringCoreProcessImpl::EventProcess(IOuringOPData *opdata, std::vector<IOuringOPData *> &postOps)
{
    if (!opdata)
        return -1;

    std::shared_ptr<BaseTransportConnection> Con = opdata->weakCon.lock();
    if (!Con) // 检查是否失效
    {
        DelNetFd(opdata->raw_ptr);
        return -1;
    }

    int res = opdata->res;
    BaseSocket fd = opdata->fd;
    IOUring_OPType OP_Type = opdata->OP_Type;

    if (fd <= 0 || !Con->ValidSocket())
    {
        return -1;
    }

    if (res < 0) // res<0连接异常断开
    {
        int err = -res;

        if (err == EAGAIN || err == EWOULDBLOCK)
        {
            std::shared_ptr<NetCore_IOuringData> iodata;
            if (_IOUringData.Find(Con.get(), iodata))
            {
                if (OP_Type == IOUring_OPType::OP_WRITE)
                {
                    iodata->NotifyIOEventRetry(opdata);
                }
                else if (OP_Type == IOUring_OPType::OP_WILLWRITE)
                {
                    iodata->NotifyIOEventRetry(opdata);
                }
                else if (OP_Type == IOUring_OPType::OP_READ)
                {
                    auto readop = IOuringOPData::CreateReadOP(Con, opdata->buffer.Length());
                    postOps.emplace_back(readop);
                }
                else if (OP_Type == IOUring_OPType::OP_ACCEPT)
                {
                    auto acceptop = IOuringOPData::CreateAcceptOP(Con);
                    postOps.emplace_back(acceptop);
                }
                return 0;
            }
            else
            {
                return -1;
            }
        }
        else
        {
            std::shared_ptr<NetCore_IOuringData> iodata;
            if (_IOUringData.Find(Con.get(), iodata) && iodata && iodata->recver)
            {
                if (iodata->recver->Active())
                {
                    iodata->recver->SetActive(false);
                    auto rdhupevent = SequentialEventExecutor::ExcuteEvent::CreateRDHUP(iodata);
                    iodata->recver->SubmitExcuteEvent(rdhupevent);
                }
            }
            else
            {
                DelNetFd(Con.get());
            }
            return -1;
        }
    }

    if (res == 0) // res==0连接正常断开
    {
        std::shared_ptr<NetCore_IOuringData> iodata;
        if (_IOUringData.Find(Con.get(), iodata) && iodata && iodata->recver)
        {
            if (iodata->recver->Active())
            {
                iodata->recver->SetActive(false);
                auto rdhupevent = SequentialEventExecutor::ExcuteEvent::CreateRDHUP(iodata);
                iodata->recver->SubmitExcuteEvent(rdhupevent);
            }
        }
        else
        {
            DelNetFd(Con.get());
        }
        return 1;
    }

    switch (OP_Type)
    {
    case IOUring_OPType::OP_WRITE:
    {
        int bufferwrite = res;
        opdata->buffer.Seek(opdata->buffer.Position() + bufferwrite);
        std::shared_ptr<NetCore_IOuringData> iodata;
        if (_IOUringData.Find(Con.get(), iodata) && iodata)
        {
            iodata->NotifyIOEventDone(opdata);
        }
        break;
    }
    case IOUring_OPType::OP_WILLWRITE:
    {
        if (Con->GetFd() == fd && Con->GetNetType() == NetType::Client)
        {
            std::shared_ptr<TCPTransportConnection> tcpCon(
                Con, static_cast<TCPTransportConnection *>(Con.get()));
            SendRes(tcpCon);
        }
        std::shared_ptr<NetCore_IOuringData> iodata;
        if (_IOUringData.Find(Con.get(), iodata) && iodata)
        {
            iodata->NotifyIOEventDone(opdata);
        }
        break;
    }
    case IOUring_OPType::OP_READ:
    {
        std::shared_ptr<NetCore_IOuringData> iodata;
        if (_IOUringData.Find(Con.get(), iodata) && iodata)
        {
            int bufferlen = res;
            if (bufferlen > 0)
            {
                iodata->state->Update(bufferlen);
                Buffer &buf = opdata->buffer;
                Buffer buffer(buf.Byte(), bufferlen);
                if (iodata->recver->Active())
                {
                    auto readevent = SequentialEventExecutor::ExcuteEvent::CreateReadEvent(iodata, buffer);
                    iodata->recver->SubmitExcuteEvent(readevent);
                }
            }

            iodata->NotifyIOEventDone(opdata);
            auto readop = IOuringOPData::CreateReadOP(Con, iodata->state->GetDynamicSize());
            postOps.emplace_back(readop);
        }
        break;
    }
    case IOUring_OPType::OP_ACCEPT:
    {
        BaseSocket client_fd = res;
        std::shared_ptr<NetCore_IOuringData> iodata;
        if (_IOUringData.Find(Con.get(), iodata) && iodata)
        {
            if (client_fd > 0 && Con->GetNetType() == NetType::Listener)
            {
                sockaddr_in client_addr = opdata->client_addr;
                if (iodata->recver->Active())
                {
                    auto acceptevent = SequentialEventExecutor::ExcuteEvent::CreateAcceptEvent(iodata, client_fd, client_addr);
                    iodata->recver->SubmitExcuteEvent(acceptevent);
                }
            }
            iodata->NotifyIOEventDone(opdata);
            auto acceptop = IOuringOPData::CreateAcceptOP(Con);
            postOps.emplace_back(acceptop);
        }
        break;
    }

    default:
        break;
    }

    return 1;
}

void IOuringCoreProcessImpl::DoPostIOEvents(std::vector<IOuringOPData *> opdatas)
{
    std::lock_guard<CriticalSectionLock> lock(_doPostIOEventLock);

    for (auto opdata : opdatas)
    {
        switch (opdata->OP_Type)
        {
        case IOUring_OPType::OP_WRITE:
        {
            SubmitWriteEvent(opdata);
        }
        break;
        case IOUring_OPType::OP_READ:
        {
            SubmitReadEvent(opdata);
        }
        break;
        case IOUring_OPType::OP_ACCEPT:
        {
            SubmitAcceptEvent(opdata);
        }
        break;
        case IOUring_OPType::OP_WILLWRITE:
        {
            SubmitWillWriteEvent(opdata);
        }
        break;

        default:
            IODATAMANAGER->ReleaseData(opdata);
            break;
        }
    }
    int submitted = io_uring_submit(&ring);
    if (submitted < 0)
    {
        std::cout << "io_uring_submit error!";
    }
    if (submitted != opdatas.size())
    {
        std::cout << "io_uring_submit diff!";
    }
}

void IOuringCoreProcessImpl::DoPostExcuteEvents(std::vector<std::shared_ptr<SequentialEventExecutor::ExcuteEvent>> &events)
{
    auto task =
        [this](std::shared_ptr<SequentialEventExecutor::ExcuteEvent> event) -> void
    {
        if (!event)
            return;

        auto iodata = event->weakdata.lock();
        if (!iodata)
            return;

        try
        {
            auto connection = iodata->weakCon.lock();
            if (event->type == SequentialEventExecutor::ExcuteEvent::EventType::READ_HUP)
                this->DelNetFd(connection.get());
            if (connection)
            {
                if (event->type == SequentialEventExecutor::ExcuteEvent::EventType::READ_DATA)
                {
                    Buffer &buf = *(event->data);
                    connection->READ(connection->GetFd(),
                                     buf);
                }
                else if (event->type == SequentialEventExecutor::ExcuteEvent::EventType::ACCEPT_CONNECTION)
                    connection->ACCEPT(connection->GetFd(),
                                       event->accept_info.client_fd, event->accept_info.client_addr);
                else if (event->type == SequentialEventExecutor::ExcuteEvent::EventType::READ_HUP)
                {
                    connection->RDHUP();
                }
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << "NetCoreCallback Error!" << e.what() << '\n';
        }

        auto recver = iodata->recver;
        if (recver)
            recver->NotifyDone();
    };
    for (auto event : events)
    {
        _ExcuteEventProcessPool.submit(task, event);
    }
}

bool IOuringCoreProcessImpl::SubmitAcceptEvent(IOuringOPData *opdata)
{
    io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    if (!sqe)
        return false;

    io_uring_prep_accept(sqe, opdata->fd, (struct sockaddr *)&(opdata->client_addr), &(opdata->addr_len), 0);
    io_uring_sqe_set_data(sqe, (void *)opdata);

    return true;
}

bool IOuringCoreProcessImpl::SubmitWriteEvent(IOuringOPData *opdata)
{
    io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    if (!sqe)
        return false;

    io_uring_prep_send(sqe, opdata->fd, opdata->buffer.Byte() + opdata->buffer.Position(), opdata->buffer.Remain(), 0);
    io_uring_sqe_set_data(sqe, (void *)opdata);

    return true;
}

bool IOuringCoreProcessImpl::SubmitReadEvent(IOuringOPData *opdata)
{
    io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    if (!sqe)
        return false;

    io_uring_prep_recv(sqe, opdata->fd, opdata->buffer.Byte(), opdata->buffer.Length(), 0);
    io_uring_sqe_set_data(sqe, (void *)opdata);

    return true;
}

bool IOuringCoreProcessImpl::SubmitWillWriteEvent(IOuringOPData *opdata)
{
    io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    if (!sqe)
        return false;

    io_uring_prep_send(sqe, opdata->fd, opdata->buffer.Data(), opdata->buffer.Length(), 0);
    io_uring_sqe_set_data(sqe, (void *)opdata);

    return true;
}

bool isNetObjectAndOnCallBack(DeleteLaterImpl *ptr)
{
    if (BaseNetWorkSession *Session = dynamic_cast<BaseNetWorkSession *>(ptr))
    {
        if (auto EndPoint = Session->GetBaseClient())
        {
            if (auto BaseCon = EndPoint->GetBaseCon())
            {
                bool result = BaseCon->isOnCallback();
                return result;
            }
        }
    }

    if (TCPEndPoint *EndPoint = dynamic_cast<TCPEndPoint *>(ptr))
    {
        if (auto BaseCon = EndPoint->GetBaseCon())
        {
            return BaseCon->isOnCallback();
        }
    }

    if (BaseTransportConnection *BaseCon = dynamic_cast<BaseTransportConnection *>(ptr))
    {
        return BaseCon->isOnCallback();
    }

    return false;
}

void IOuringCoreProcessImpl::ProcessPendingDeletions()
{
    std::vector<DeleteLaterImpl *> deletions;
    _pendingDeletions.EnsureCall(
        [&deletions](std::vector<DeleteLaterImpl *> &array) -> void
        {
            deletions.swap(array); // 取出待删除任务
        }

    );
    for (auto ptr : deletions)
    {
        if (ptr)
        {
            // 由于线程池是异步回调,所以这里删除的时候需要检查是否在执行回调
            // 回调中的连接，退避处理
            if (isNetObjectAndOnCallBack(ptr))
            {
                _pendingDeletions.emplace(ptr);
                continue;
            }
        }
        SAFE_DELETE(ptr);
    }
}

void IOuringCoreProcessImpl::AddPendingDeletion(DeleteLaterImpl *ptr)
{
    _pendingDeletions.emplace(ptr);
}

IOuringCoreProcess *IOuringCoreProcess::Instance()
{
    static IOuringCoreProcess *m_instance = new IOuringCoreProcess();
    return m_instance;
}

IOuringCoreProcess::IOuringCoreProcess()
{
    pImpl = std::make_unique<IOuringCoreProcessImpl>();
}

int IOuringCoreProcess::Run() { return pImpl->Run(); }
void IOuringCoreProcess::Stop() { pImpl->Stop(); }
bool IOuringCoreProcess::Running() { return pImpl->Running(); }
bool IOuringCoreProcess::AddNetFd(std::shared_ptr<BaseTransportConnection> Con) { return pImpl->AddNetFd(Con); }
bool IOuringCoreProcess::DelNetFd(BaseTransportConnection *Con) { return pImpl->DelNetFd(Con); }
bool IOuringCoreProcess::SendRes(std::shared_ptr<BaseTransportConnection> BaseCon) { return pImpl->SendRes(BaseCon); }
void IOuringCoreProcess::AddPendingDeletion(DeleteLaterImpl *ptr) { return pImpl->AddPendingDeletion(ptr); }
