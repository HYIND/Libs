#include "Core/IOuringCore.h"
#include "Core/NetCoredef.h"
#include "ResourcePool.h"

#define ENTRIES 500

#define RECVBUFFERMINLEN 1024
#define RECVBUFFERDEFLEN 1024 * 5
#define RECVBUFFERMAXLEN 1024 * 1024

static void setnonblocking(int fd)
{
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
}

enum class IOUring_OPType
{
    OP_READ = 1,
    OP_WRITE = 2,
    OP_ACCEPT = 3,
    OP_WILLWRITE
};
struct IOuringOPData
{
    IOUring_OPType OP_Type;
    int fd;
    BaseTransportConnection *Con;

    Buffer buffer;

    socklen_t addr_len;
    sockaddr_in client_addr; // 客户端地址

    IOuringOPData() : IOuringOPData(IOUring_OPType::OP_READ)
    {
    }
    IOuringOPData(IOUring_OPType OP_Type)
        : OP_Type(OP_Type)
    {
        fd = 0;
        Con = nullptr;
        addr_len = 0;
        memset(&client_addr, 0, sizeof(client_addr));
    }
    ~IOuringOPData()
    {
        Reset();
    }
    void Reset()
    {
        Con = nullptr;
        buffer.Release();
        addr_len = 0;
        memset(&client_addr, 0, sizeof(client_addr));
    }
};

class IODataManager : public ResPool<IOuringOPData>
{
public:
    static IODataManager *Instance()
    {
        static IODataManager *Instance = new IODataManager();
        return Instance;
    }
    void ResetData(IOuringOPData *data)
    {
        data->Reset();
    }
    IOuringOPData *AllocateData(IOUring_OPType OP_Type, int fd, BaseTransportConnection *Con, uint32_t buffersize = RECVBUFFERDEFLEN)
    {
        IOuringOPData *data = ResPool<IOuringOPData>::AllocateData();
        data->OP_Type = OP_Type;
        data->fd = fd;
        data->Con = Con;
        if (OP_Type == IOUring_OPType::OP_READ || OP_Type == IOUring_OPType::OP_WRITE)
            data->buffer.ReSize(buffersize);
        else if (OP_Type == IOUring_OPType::OP_ACCEPT)
        {
            data->addr_len = 0;
            memset(&data->client_addr, 0, sizeof(data->client_addr));
        }
        return data;
    }
};
#define IODATAMANAGER IODataManager::Instance()

IOuringCoreProcess::SequentialSender::SequentialSender()
{
    _state = sendstate::idle;
}

IOuringCoreProcess::SequentialSender::~SequentialSender()
{
    Release();
}

void IOuringCoreProcess::SequentialSender::Release()
{
    _lock.Enter();

    _state = sendstate::none;

    while (!_queue.empty())
    {
        Buffer *buf = nullptr;
        _queue.dequeue(buf);
        SAFE_DELETE(buf);
    }

    _lock.Leave();
}

void IOuringCoreProcess::SequentialSender::Send(Buffer &buf, BaseTransportConnection *Con, int fd)
{
    if (_state == sendstate::none)
        return;

    if (fd != Con->GetFd())
        return;

    Buffer *temp = new Buffer();
    temp->QuoteFromBuf(buf);

    _queue.enqueue(temp);
    if (!_lock.TryEnter())
        return;

    ProcessQueue(Con);

    _lock.Leave();
}

void IOuringCoreProcess::SequentialSender::Update(BaseTransportConnection *Con, uint32_t sendsize, Buffer &buf)
{
    if (_state == sendstate::none)
        return;

    if (!_lock.TryEnter())
        return;

    buf.Seek(buf.Postion() + sendsize);
    if (buf.Remaind())
    {
        IOuringCoreProcess::Instance()->postSendReq(Con, buf);
        _state = sendstate::sending;
    }
    else
    {
        _state = sendstate::idle;
        ProcessQueue(Con);
    }
    _lock.Leave();
}

void IOuringCoreProcess::SequentialSender::Retry(BaseTransportConnection *Con, Buffer &buf)
{
    if (_state == sendstate::none)
        return;

    if (!_lock.TryEnter())
        return;

    if (buf.Remaind())
    {
        IOuringCoreProcess::Instance()->postSendReq(Con, buf);
        _state = sendstate::sending;
    }
    else
    {
        _state = sendstate::idle;
        ProcessQueue(Con);
    }
    _lock.Leave();
}

void IOuringCoreProcess::SequentialSender::ProcessQueue(BaseTransportConnection *Con)
{
    Buffer *sendbuf = nullptr;
    while (_queue.front(sendbuf) && _state == sendstate::idle)
    {
        if (_queue.dequeue(sendbuf))
        {
            if (!sendbuf)
                continue;
            IOuringCoreProcess::Instance()->postSendReq(Con, *sendbuf);
            _state = sendstate::sending;
        }
    }
}

IOuringCoreProcess::DynamicBufferState::DynamicBufferState()
{
    lastbuffersize = RECVBUFFERDEFLEN;
}

void IOuringCoreProcess::DynamicBufferState::Update(uint32_t newbufferrecvlen)
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

uint32_t IOuringCoreProcess::DynamicBufferState::GetDynamicSize()
{
    return lastbuffersize;
}

IOuringCoreProcess *IOuringCoreProcess::Instance()
{
    static IOuringCoreProcess *m_Instance = new IOuringCoreProcess();
    return m_Instance;
}

IOuringCoreProcess::IOuringCoreProcess()
{
    memset(&ring, 0, sizeof(ring));
    int ret = io_uring_queue_init(ENTRIES, &ring, 0);
    if (ret < 0)
    {
        std::cout << "io_uring_queue_init fail!\n";
    }
}

int IOuringCoreProcess::Run()
{
    try
    {
        _isrunning = true;
        std::thread EventLoop(&IOuringCoreProcess::Loop, this);
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

bool IOuringCoreProcess::Running()
{
    return _isrunning;
}

bool IOuringCoreProcess::AddNetFd(BaseTransportConnection *Con)
{
    // cout << "AddNetFd fd :" << Con->GetFd() << endl;
    if (Con->GetFd() <= 0)
    {
        return false;
    }
    std::shared_ptr<NetCore_IOuringData> data = std::make_shared<NetCore_IOuringData>();
    data->fd = Con->GetFd();
    data->Con = Con;
    data->sender = std::make_shared<SequentialSender>();
    data->state = std::make_shared<DynamicBufferState>();
    _IOUringData.Insert(Con, data);

    if (Con->GetNetType() == NetType::Client)
    {
        setnonblocking(Con->GetFd());
        if (!postRecvReq(Con))
        {
            DelNetFd(Con);
            return false;
        }
    }
    if (Con->GetNetType() == NetType::Listener)
    {
        if (!postAcceptReq(Con))
        {
            DelNetFd(Con);
            return false;
        }
    }
    return true;
}

bool IOuringCoreProcess::DelNetFd(BaseTransportConnection *Con)
{
    // IODATAMANAGER->CancelIOEvent(Con);

    std::shared_ptr<NetCore_IOuringData> data;
    if (_IOUringData.Find(Con, data))
        _IOUringData.Erase(Con);

    return true;
}

bool IOuringCoreProcess::SendRes(TCPTransportConnection *Con)
{
    if (!Con->GetSendMtx().try_lock())
        return true; // 写锁正在被其他线程占用

    int fd = Con->GetFd();
    SafeQueue<Buffer *> &SendDatas = Con->GetSendData();

    std::shared_ptr<NetCore_IOuringData> iodata;
    if (!_IOUringData.Find(Con, iodata))
    {
        iodata = std::make_shared<NetCore_IOuringData>();
        iodata->fd = Con->GetFd();
        iodata->Con = Con;
        iodata->sender = std::make_shared<SequentialSender>();
        iodata->state = std::make_shared<DynamicBufferState>();
        _IOUringData.Insert(Con, iodata);
    }

    int count = 0;
    while (count < 5 && !SendDatas.empty())
    {

        Buffer *buffer = nullptr;
        if (!SendDatas.front(buffer))
            break;

        if (!buffer->Data() || buffer->Length() <= 0)
        {
            // 无效Buffer不发送
        }
        else
        {
            iodata->sender->Send(*buffer, Con, fd);
        }
        SendDatas.dequeue(buffer);
        SAFE_DELETE(buffer);
        count++;
    }

    if (!SendDatas.empty()) // 仍有数据未发送,关注其可写事件,等待下次可写事件
    {
        postWillSendReq(Con);
    }

    Con->GetSendMtx().unlock();
    return true;
}

void IOuringCoreProcess::Loop()
{
    std::cout << "IOuringCore , EventLoop\n";

    while (_isrunning)
    {

        // 非阻塞检测
        unsigned ready = io_uring_cq_ready(&ring);
        if (ready > 0)
        {
            static unsigned max_batch = 100;
            unsigned batch_size = std::min(max_batch, ready);

            io_uring_cqe *cqes[100];
            int cqecount = io_uring_peek_batch_cqe(&ring, cqes, batch_size);
            if (cqecount > 0)
            {
                for (int i = 0; i < cqecount; i++)
                {
                    EventProcess(cqes[i]);
                }
                io_uring_cq_advance(&ring, cqecount);
            }
        }
        else
        {
            static int milliseconds = 300;
            static __kernel_timespec ts{milliseconds / 1000, (milliseconds % 1000) * 1000000};

            // 没有完成事件，等待一个
            struct io_uring_cqe *cqe = nullptr;
            int ret = io_uring_wait_cqe_timeout(&ring, &cqe, &ts);

            if (ret == 0 && cqe)
            {
                EventProcess(cqe);
                io_uring_cqe_seen(&ring, cqe);
            }
            else if (ret == -ETIME)
            {
                // 超时，正常返回
            }
            else if (ret < 0 && ret != -EINTR)
            {
                std::cout << "io_uring handle error!";
            }
        }
        // io_uring_cqe *cqe = nullptr;
        // int ret = io_uring_wait_cqe(&ring, &cqe);
        // if (ret >= 0)
        // {
        // }
        // else
        // {
        // }
        ProcessPendingDeletions();
    }
    io_uring_queue_exit(&ring);
}

int IOuringCoreProcess::EventProcess(io_uring_cqe *cqe)
{
    if (!cqe)
        return -1;

    IOuringOPData *data = (IOuringOPData *)io_uring_cqe_get_data(cqe);
    int res = cqe->res;

    int fd = data->fd;
    BaseTransportConnection *Con = data->Con;
    IOUring_OPType OP_Type = data->OP_Type;

    if (fd <= 0 || !Con->ValidSocket())
    {
        IODATAMANAGER->ReleaseData(data);
        return -1;
    }

    if (res < 0) // res<0连接异常断开
    {
        int err = -res;

        if (err == EAGAIN || err == EWOULDBLOCK)
        {
            if (OP_Type == IOUring_OPType::OP_WRITE)
            {
                std::shared_ptr<NetCore_IOuringData> iodata;
                if (_IOUringData.Find(Con, iodata))
                {
                    if (iodata)
                    {
                        Buffer &buf = data->buffer;
                        iodata->sender->Retry(Con, buf);
                    }
                }
                IODATAMANAGER->ReleaseData(data);
                return 0;
            }
            // 对于READ/ACCEPT的EAGAIN，重新提交请求
            else if (OP_Type == IOUring_OPType::OP_READ)
            {
                IODATAMANAGER->ReleaseData(data);
                postRecvReq(Con);
                return 0;
            }
            else if (OP_Type == IOUring_OPType::OP_ACCEPT)
            {
                IODATAMANAGER->ReleaseData(data);
                postAcceptReq(Con);
                return 0;
            }
        }
        else
        {
            IODATAMANAGER->ReleaseData(data);
            DelNetFd(Con);
            Con->OnRDHUP();
            return -1;
        }
    }

    if (res == 0) // res==0连接正常断开
    {
        IODATAMANAGER->ReleaseData(data);
        DelNetFd(Con);
        Con->OnRDHUP();
        return 1;
    }

    switch (OP_Type)
    {
    case IOUring_OPType::OP_READ:
    {
        if (!Con)
            break;

        int bufferlen = res;
        if (bufferlen > 0)
        {
            UpdateDynamicBufferState(Con, bufferlen);
            Buffer &buf = data->buffer;
            Buffer buffer(buf.Byte(), bufferlen);
            Con->OnREAD(fd, buffer);
        }
        postRecvReq(Con);
        break;
    }
    case IOUring_OPType::OP_WRITE:
    {
        if (!Con)
            break;

        int bufferwrite = res;
        std::shared_ptr<NetCore_IOuringData> iodata;
        if (_IOUringData.Find(Con, iodata))
        {
            if (iodata)
            {
                Buffer &buf = data->buffer;
                iodata->sender->Update(Con, bufferwrite, buf);
            }
        }
        break;
    }
    case IOUring_OPType::OP_ACCEPT:
    {
        if (!Con)
            break;

        int client_fd = res;
        if (client_fd > 0)
        {
            sockaddr_in client_addr = data->client_addr;
            Con->OnACCEPT(fd, client_fd, client_addr);
        }
        postAcceptReq(Con);
        break;
    }
    case IOUring_OPType::OP_WILLWRITE:
    {
        if (Con->GetFd() == fd && Con->GetNetType() == NetType::Client)
        {
            SendRes((TCPTransportConnection *)Con);
        }
        break;
    }
    default:
        break;
    }

    IODATAMANAGER->ReleaseData(data);
    return 1;
}

bool IOuringCoreProcess::postAcceptReq(BaseTransportConnection *Con)
{
    io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    if (!sqe)
        return false;

    IOuringOPData *data = IODATAMANAGER->AllocateData(IOUring_OPType::OP_ACCEPT, Con->GetFd(), Con, 0);

    data->addr_len = sizeof(sockaddr_in);
    io_uring_prep_accept(sqe, Con->GetFd(), (struct sockaddr *)&(data->client_addr), &(data->addr_len), 0);
    io_uring_sqe_set_data(sqe, (void *)data);

    int submitted = io_uring_submit(&ring);
    if (submitted < 0)
    {
        IODATAMANAGER->ReleaseData(data);
        return false;
    }
    return true;
}

bool IOuringCoreProcess::postSendReq(BaseTransportConnection *Con, Buffer &buf)
{
    io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    if (!sqe)
        return false;

    IOuringOPData *data = IODATAMANAGER->AllocateData(IOUring_OPType::OP_WRITE, Con->GetFd(), Con, 0);
    int oripos = buf.Postion();
    data->buffer.QuoteFromBuf(buf);
    data->buffer.Seek(oripos);

    io_uring_prep_send(sqe, Con->GetFd(), data->buffer.Data() + data->buffer.Postion(), data->buffer.Remaind(), 0);
    io_uring_sqe_set_data(sqe, (void *)data);

    int submitted = io_uring_submit(&ring);
    if (submitted < 0)
    {
        IODATAMANAGER->ReleaseData(data);
        return false;
    }
    return true;
}

bool IOuringCoreProcess::postRecvReq(BaseTransportConnection *Con)
{
    io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    if (!sqe)
        return false;

    uint32_t size = GetDynamicBufferSize(Con);

    IOuringOPData *data = IODATAMANAGER->AllocateData(IOUring_OPType::OP_READ, Con->GetFd(), Con, size);
    io_uring_prep_recv(sqe, Con->GetFd(), data->buffer.Data(), data->buffer.Length(), 0);
    io_uring_sqe_set_data(sqe, (void *)data);

    int submitted = io_uring_submit(&ring);
    if (submitted < 0)
    {
        IODATAMANAGER->ReleaseData(data);
        return false;
    }
    return true;
}

bool IOuringCoreProcess::postWillSendReq(BaseTransportConnection *Con)
{
    io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    if (!sqe)
        return false;

    IOuringOPData *data = IODATAMANAGER->AllocateData(IOUring_OPType::OP_WILLWRITE, Con->GetFd(), Con, 0);

    io_uring_prep_send(sqe, Con->GetFd(), data->buffer.Data(), data->buffer.Length(), 0);
    io_uring_sqe_set_data(sqe, (void *)data);

    int submitted = io_uring_submit(&ring);
    if (submitted < 0)
    {
        IODATAMANAGER->ReleaseData(data);
        return false;
    }
    return true;
}

void IOuringCoreProcess::UpdateDynamicBufferState(BaseTransportConnection *Con, uint32_t newbufferrecvlen)
{
    std::shared_ptr<NetCore_IOuringData> iodata;
    if (_IOUringData.Find(Con, iodata))
        iodata->state->Update(newbufferrecvlen);
}
uint32_t IOuringCoreProcess::GetDynamicBufferSize(BaseTransportConnection *Con)
{
    std::shared_ptr<NetCore_IOuringData> iodata;
    if (_IOUringData.Find(Con, iodata))
        return iodata->state->GetDynamicSize();
    return RECVBUFFERMINLEN;
}

void IOuringCoreProcess::ProcessPendingDeletions()
{
    std::vector<DeleteLaterImpl *> deletions;
    _pendingDeletions.EnsureCall(
        [&](std::vector<DeleteLaterImpl *> &array) -> void
        {
            deletions.swap(array); // 取出待删除任务
        }

    );
    for (auto ptr : deletions)
    {
        SAFE_DELETE(ptr);
    }
}

void IOuringCoreProcess::AddPendingDeletion(DeleteLaterImpl *ptr)
{
    _pendingDeletions.emplace(ptr);
}
