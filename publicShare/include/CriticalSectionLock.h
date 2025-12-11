#pragma once

#ifdef __linux__
#include <pthread.h>
#include <mutex>
#endif

class CriticalSectionLock
{
public:
    CriticalSectionLock();
    ~CriticalSectionLock();
    bool TryEnter();
    void Enter();
    void Leave();

    // 适配std::lock_guard
public:
    void lock();
    void unlock();

private:
#ifdef __linux__
    pthread_mutex_t _mutex;
    pthread_mutexattr_t _attr;
#endif
};
