#include "SpinLock.h"

SpinLock::SpinLock()
    : _flag(ATOMIC_FLAG_INIT)
{
}

bool SpinLock::trylock()
{
    return !_flag.test_and_set();
}
void SpinLock::lock()
{ // acquire spin lock
    while (_flag.test_and_set(std::memory_order_acquire))
    {
    }
}
void SpinLock::unlock()
{ // release spin lock
    _flag.clear(std::memory_order_release);
}