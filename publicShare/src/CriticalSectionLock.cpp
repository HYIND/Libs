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

#ifdef __linux__
SharedLock::SharedLock()
{
}
SharedLock::~SharedLock()
{
}

bool SharedLock::try_lock() noexcept
{
	return _mutex.try_lock();
}
void SharedLock::lock() noexcept
{
	_mutex.lock();
}
void SharedLock::unlock() noexcept
{
	_mutex.unlock();
}

bool SharedLock::try_lock_shared() noexcept
{
	return _mutex.try_lock_shared();
}

void SharedLock::lock_shared() noexcept
{
	_mutex.lock_shared();
}

void SharedLock::unlock_shared() noexcept
{
	_mutex.unlock_shared();
}
#elif defined(_WIN32)
SharedLock::SharedLock()
{
	_srwlock = SRWLOCK_INIT;
}
SharedLock::~SharedLock()
{
}

bool SharedLock::try_lock() noexcept
{
	return TryAcquireSRWLockExclusive(&_srwlock);
}
void SharedLock::lock() noexcept
{
	AcquireSRWLockExclusive(&_srwlock);
}
void SharedLock::unlock() noexcept
{
	ReleaseSRWLockExclusive(&_srwlock);
}

bool SharedLock::try_lock_shared() noexcept
{
	return TryAcquireSRWLockShared(&_srwlock);
}

void SharedLock::lock_shared() noexcept
{
	AcquireSRWLockShared(&_srwlock);
}

void SharedLock::unlock_shared() noexcept
{
	ReleaseSRWLockShared(&_srwlock);
}
#endif
