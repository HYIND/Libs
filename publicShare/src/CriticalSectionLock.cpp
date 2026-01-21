#include "CriticalSectionLock.h"

#ifdef __linux__
CriticalSectionLock::CriticalSectionLock() : _attr()
{
    pthread_mutexattr_init(&_attr);
    pthread_mutexattr_settype(&_attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&_mutex, &_attr);
    pthread_mutexattr_setpshared(&_attr, PTHREAD_PROCESS_PRIVATE);
}

CriticalSectionLock::~CriticalSectionLock()
{
    pthread_mutexattr_destroy(&_attr);
}

bool CriticalSectionLock::TryEnter()
{
    return pthread_mutex_trylock(&_mutex) == 0;
}

void CriticalSectionLock::Enter()
{
    pthread_mutex_lock(&_mutex);
}

void CriticalSectionLock::Leave()
{
    pthread_mutex_unlock(&_mutex);
}

#elif defined(_WIN32)

CriticalSectionLock::CriticalSectionLock()
{
    InitializeCriticalSection(&_cs);
}

CriticalSectionLock::~CriticalSectionLock()
{
    DeleteCriticalSection(&_cs);
}

bool CriticalSectionLock::TryEnter()
{
    return TryEnterCriticalSection(&_cs) != 0;
}

void CriticalSectionLock::Enter()
{
    EnterCriticalSection(&_cs);
}

void CriticalSectionLock::Leave()
{
    LeaveCriticalSection(&_cs);
}

#endif

bool CriticalSectionLock::try_lock()
{
    return TryEnter();
}

void CriticalSectionLock::lock()
{
    Enter();
}

void CriticalSectionLock::unlock()
{
    Leave();
}

LockGuard::LockGuard(CriticalSectionLock &lock, bool istrylock)
    : _lock(lock), _isownlock(false)
{
    if (!istrylock)
    {
        this->lock();
    }
    else
    {
        _isownlock = _lock.TryEnter();
    }
}

bool LockGuard::isownlock()
{
    return _isownlock;
}

LockGuard::~LockGuard()
{
    unlock();
}

void LockGuard::lock()
{
    if (_isownlock)
        return;
    _lock.Enter();
    _isownlock = true;
}

void LockGuard::unlock()
{
    if (_isownlock)
        _lock.Leave();
}

void ConditionVariable::Wait(LockGuard &lock)
{
    _cv.wait(lock);
}

bool ConditionVariable::WaitFor(LockGuard &lock, const std::chrono::microseconds ms)
{
    return _cv.wait_for(lock, ms) == std::cv_status::timeout;
}

void ConditionVariable::NotifyAll()
{
    _cv.notify_all();
}

void ConditionVariable::NotifyOne()
{
    _cv.notify_one();
}
