#pragma once

#include <sys/socket.h>
#include <netinet/in.h>
#include <liburing.h>
#include "Core/TCPTransportWarpper.h"
#include "Core/DeleteLater.h"
#include "ThreadPool.h"

struct IOuringOPData;

enum class IOUring_OPType;

class IOuringCoreProcess
{

    class SequentialIOSubmitter;
    class SequentialEventExecutor;
    class DynamicBufferState;

private:
    struct NetCore_IOuringData
    {
        int fd;
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
        SequentialIOSubmitter(IOuringCoreProcess *core, std::shared_ptr<NetCore_IOuringData> &data, IOUring_OPType type);
        ~SequentialIOSubmitter();
        void Release();
        void SubmitOPdata(IOuringOPData *opdata);
        void NotifyRetry(IOuringOPData *opdata);
        void NotifyDone(IOuringOPData *opdata);
        IOUring_OPType GetSubmitType();
        void GetPostIOEvent(std::vector<IOuringOPData *> &out);

    private:
        IOuringCoreProcess *_core;
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
        SequentialEventExecutor(IOuringCoreProcess *_core, std::shared_ptr<NetCore_IOuringData> &data);
        ~SequentialEventExecutor();
        void Release();
        void SubmitExcuteEvent(std::shared_ptr<ExcuteEvent> event);
        void NotifyDone();
        void GetPostExcuteEvent(std::vector<std::shared_ptr<ExcuteEvent>> &out);

        // void SubmitReadEvent(Buffer &buf);
        // void SubmitAcceptEvent(int clientfd, sockaddr_in addr);
        // void SubmitRDHUPEvent();

        // private:
        //     void ProcessQueue();

    private:
        IOuringCoreProcess *_core;
        std::weak_ptr<NetCore_IOuringData> _weakdata;
        CriticalSectionLock _lock;
        SafeQueue<std::shared_ptr<ExcuteEvent>> _queue;
        excutestate _state;
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
    static IOuringCoreProcess *Instance();
    int Run();
    void Stop();
    bool Running();

public:
    bool AddNetFd(std::shared_ptr<BaseTransportConnection> Con);
    bool DelNetFd(BaseTransportConnection *Con);
    bool SendRes(std::shared_ptr<BaseTransportConnection> BaseCon);
    void AddPendingDeletion(DeleteLaterImpl *ptr);

private:
    IOuringCoreProcess();
    void LoopSubmitIOEvent();
    void LoopSubmitExcuteEvent();
    void Loop();
    bool GetDoneIOEvents(std::vector<IOuringOPData *> &opdatas);
    int EventProcess(IOuringOPData *opdata, std::vector<IOuringOPData *> &postOps);
    void ThreadEnd();
    void ProcessPendingDeletions();

private:
    void DoPostIOEvents(std::vector<IOuringOPData *> opdatas);
    void DoPostExcuteEvents(std::vector<std::shared_ptr<SequentialEventExecutor::ExcuteEvent>> &events);

    bool SubmitWriteEvent(IOuringOPData *opdata);
    bool SubmitReadEvent(IOuringOPData *opdata);
    bool SubmitAcceptEvent(IOuringOPData *opdata);
    bool SubmitWillWriteEvent(IOuringOPData *opdata);

private:
    bool _isrunning = false;
    io_uring ring;

    SafeMap<BaseTransportConnection *, std::shared_ptr<NetCore_IOuringData>> _IOUringData;
    SafeArray<DeleteLaterImpl *> _pendingDeletions;
    ThreadPool _ExcuteEventProcessPool;

    std::mutex _IOEventLock;
    std::condition_variable _IOEventCV;

    std::mutex _ExcuteLock;
    std::condition_variable _ExcuteCV;

    CriticalSectionLock _doPostIOEventLock;
};