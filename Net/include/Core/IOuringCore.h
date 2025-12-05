#pragma once

#include <sys/socket.h>
#include <netinet/in.h>
#include <liburing.h>
#include "Core/TCPTransportWarpper.h"
#include "Core/DeleteLater.h"

enum class sendstate
{
    none = -1,
    idle = 0,
    sending = 1
};

class IOuringCoreProcess
{

public:
    static IOuringCoreProcess *Instance();
    int Run();
    bool Running();

public:
    bool AddNetFd(BaseTransportConnection *Con);
    bool DelNetFd(BaseTransportConnection *Con);
    bool SendRes(TCPTransportConnection *Con);
    void AddPendingDeletion(DeleteLaterImpl *ptr);

private:
    IOuringCoreProcess();
    void Loop();
    int EventProcess(io_uring_cqe *cqe);
    void ThreadEnd();
    void ProcessPendingDeletions();

private:
    bool postAcceptReq(BaseTransportConnection *Con);
    bool postSendReq(BaseTransportConnection *Con, Buffer &buf);
    bool postRecvReq(BaseTransportConnection *Con);
    bool postWillSendReq(BaseTransportConnection *Con);
    void UpdateDynamicBufferState(BaseTransportConnection *Con, uint32_t newbufferrecvlen);
    uint32_t GetDynamicBufferSize(BaseTransportConnection *Con);

private:
    class SequentialSender
    {

    public:
        SequentialSender();
        ~SequentialSender();
        void Release();
        void Send(Buffer &buf, BaseTransportConnection *Con, int fd);
        void Update(BaseTransportConnection *Con, uint32_t sendsize, Buffer &buf);
        void Retry(BaseTransportConnection *Con, Buffer &buf);

    private:
        void ProcessQueue(BaseTransportConnection *Con);

    private:
        CriticalSectionLock _lock;
        SafeQueue<Buffer *> _queue;
        sendstate _state;
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

    struct NetCore_IOuringData
    {
        int fd;
        BaseTransportConnection *Con;
        std::shared_ptr<SequentialSender> sender;
        std::shared_ptr<DynamicBufferState> state;
    };

private:
    bool _isrunning = false;
    io_uring ring;

    SafeMap<BaseTransportConnection *, std::shared_ptr<NetCore_IOuringData>> _IOUringData;

    SafeArray<DeleteLaterImpl *> _pendingDeletions;
};