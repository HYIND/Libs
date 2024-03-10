#include "CriticalSectionLock.h"
#include <error.h>

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

bool CriticalSectionLock::Leave()
{
    pthread_mutex_unlock(&_mutex);
    return false;
}
#endif
