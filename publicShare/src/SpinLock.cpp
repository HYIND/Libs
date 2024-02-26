#include "SpinLock.h"

bool SpinLock::trylock()
{
    return !_flag.test_and_set();
}
void SpinLock::lock()
{ // acquire spin lock
    while (_flag.test_and_set())
    {
    }
}
void SpinLock::unlock()
{ // release spin lock
    _flag.clear();
}