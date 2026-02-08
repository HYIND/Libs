#pragma once

#include "Coroutine.h"
#include "ThreadPool.h"

struct Coro_IOCPOPData;

class CoroutineScheduler
{
public:
    static CoroutineScheduler *Instance();
    int Run();
    void Stop();
    bool Running();

public: // Task
    std::shared_ptr<TaskHandle> RegisterTaskCoroutine(std::coroutine_handle<> coroutine);

public: // Connect
    std::shared_ptr<CoConnection::Handle> create_connection(BaseSocket socket, sockaddr_in localaddr, sockaddr_in remoteaddr);

private:
    CoroutineScheduler();
    ~CoroutineScheduler();
    void LoopSubmitIOEvent();
    void Loop();
    bool GetDoneIOEvents(std::vector<Coro_IOCPOPData *> &opdatas);
    int EventProcess(Coro_IOCPOPData *opdata);
    bool AddReadShutDownEvent(Coro_IOCPOPData *opdata);
    void DoPostIOEvents(std::vector<Coro_IOCPOPData *> &opdatas);

private:
    bool SubmitTimerEvent(Coro_IOCPOPData *opdata);
    bool SubmitCoroutineEvent(Coro_IOCPOPData *opdata);
    bool SubmitConnectEvent(Coro_IOCPOPData *opdata);

    bool AssociateSocketWithIOCP(HANDLE handle, ULONG_PTR completionKey);

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

    HANDLE _iocp;

    SafeQueue<Coro_IOCPOPData *> _optaskqueue;

    ThreadPool _ExcuteEventProcessPool;

    std::mutex _IOEventLock;
    std::condition_variable _IOEventCV;

    CriticalSectionLock _doPostIOEventLock;
};