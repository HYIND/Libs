#pragma once

#ifdef __linux__
#include <pthread.h>
#endif

class CriticalSectionLock
{
public:
    CriticalSectionLock();
    ~CriticalSectionLock();
    bool TryEnter();
    void Enter();
    bool Leave();

private:
#ifdef __linux__
    pthread_mutex_t _mutex;
    pthread_mutexattr_t _attr;
#endif
};
