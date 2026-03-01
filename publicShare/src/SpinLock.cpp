#include "SpinLock.h"
#include <stdexcept>

#include "SpinLock.h"

#ifdef __linux__
SpinLock::SpinLock()
	: _flag(ATOMIC_FLAG_INIT)
{
}
#else
SpinLock::SpinLock()
	: _flag()
{
}
#endif

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

#ifdef __linux__
RecursiveSpinLock::RecursiveSpinLock()
	: _flag(ATOMIC_FLAG_INIT)
{
}
#else
RecursiveSpinLock::RecursiveSpinLock()
	: _flag()
{
}
#endif

bool RecursiveSpinLock::trylock()
{
	auto current_id = std::this_thread::get_id();
	if (_owner == current_id)
	{
		_recursion_count++;
		return true;
	}

	if (!_flag.test_and_set())
	{
		_owner = current_id;
		_recursion_count = 1;
		return true;
	}

	return false;
}
void RecursiveSpinLock::lock()
{
	auto current_id = std::this_thread::get_id();
	if (_owner == current_id)
	{
		_recursion_count++;
		return;
	}

	while (_flag.test_and_set(std::memory_order_acquire))
	{
	}

	_owner = current_id;
	_recursion_count = 1;
}
void RecursiveSpinLock::unlock()
{
	if (_owner != std::this_thread::get_id()) {
		throw std::runtime_error("Current Thread");
		return;
	}

	if (--_recursion_count == 0) {
		_owner = std::thread::id();
		_flag.clear(std::memory_order_release);
	}
}

bool RecursiveSpinLock::is_locked() const {
	return _owner != std::thread::id();
}

int RecursiveSpinLock::recursion_count() const {
	return _recursion_count;
}