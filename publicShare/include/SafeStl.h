#pragma once

#include <map>
#include <queue>
#include <functional>
#include <iostream>
#include "CriticalSectionLock.h"

template <typename K, typename V>
class SafeMap
{
public:
	SafeMap() {}

	~SafeMap() {}

	SafeMap(const SafeMap &rhs)
	{
		_map = rhs._map;
	}

	SafeMap &operator=(const SafeMap &rhs)
	{
		_lock.Enter();
		if (&rhs != this)
		{
			_map = rhs._map;
		}
		_lock.Leave();
		return *this;
	}

	V &operator[](const K &key)
	{
		_lock.Enter();
		V result = _map[key];
		_lock.Leave();
	}

	int Size()
	{
		_lock.Enter();
		int result = _map.size();
		_lock.Leave();
		return result;
	}

	bool IsEmpty()
	{
		_lock.Enter();
		bool result = _map.empty();
		_lock.Leave();
		return result;
	}

	bool Insert(const K &key, const V &value)
	{
		_lock.Enter();
		if (_map.find(key) != _map.end())
			return false;
		auto ret = _map.insert(std::pair<K, V>(key, value));
		_lock.Leave();
		return true;
	}

	void EnsureInsert(const K &key, const V &value)
	{
		_lock.Enter();
		auto ret = _map.insert(std::pair<K, V>(key, value));
		// find key and cannot insert
		if (!ret.second)
		{
			_map.erase(ret.first);
			_map.insert(std::pair<K, V>(key, value));
			return;
		}
		_lock.Leave();
		return;
	}

	bool Find(const K &key, V &value)
	{
		_lock.Enter();
		bool ret = false;

		auto iter = _map.find(key);
		if (iter != _map.end())
		{
			value = iter->second;
			ret = true;
		}

		_lock.Leave();
		return ret;
	}

	bool FindOldAndSetNew(const K &key, V &oldValue, const V &newValue)
	{
		_lock.Enter();
		bool ret = false;

		if (_map.size() > 0)
		{
			auto iter = _map.find(key);
			if (iter != _map.end())
			{
				oldValue = iter->second;
				_map.erase(iter);
				_map.insert(std::pair<K, V>(key, newValue));
				ret = true;
			}
		}

		_lock.Leave();
		return ret;
	}

	void Erase(const K &key)
	{
		_lock.Enter();
		_map.erase(key);
		_lock.Leave();
	}

	void Clear()
	{
		_lock.Enter();
		_map.clear();
		_lock.Leave();
		return;
	}

	void EnsureCall(std::function<void(std::map<K, V> &map)> callback)
	{
		if (callback)
		{
			_lock.Enter();
			callback(this->_map);
			_lock.Leave();
		}
	}

private:
	std::map<K, V> _map;
	CriticalSectionLock _lock;
};

template <typename T>
class SafeQueue
{
private:
	std::queue<T> _queue;
	CriticalSectionLock _lock;

public:
	SafeQueue() {}
	SafeQueue(SafeQueue &&other) { _queue = other._queue; }
	~SafeQueue() {}
	bool empty()
	{
		_lock.Enter();
		bool result = _queue.empty();
		_lock.Leave();
		return result;
	}
	int size()
	{
		_lock.Enter();
		int result = _queue.size();
		_lock.Leave();
		return result;
	}
	// 队列添加元素
	void enqueue(T &t)
	{
		_lock.Enter();
		_queue.emplace(t);
		_lock.Leave();
	}
	// 队列取出元素
	bool dequeue(T &t)
	{
		_lock.Enter();
		if (_queue.empty())
		{
			_lock.Leave();
			return false;
		}
		t = std::move(_queue.front());
		_queue.pop();
		_lock.Leave();
		return true;
	}
	// 查看队列首元素
	bool front(T &t)
	{
		_lock.Enter();
		if (_queue.empty())
		{
			_lock.Leave();
			return false;
		}
		t = std::move(_queue.front());
		_lock.Leave();
		return true;
	}
	// 查看队列尾元素
	bool back(T &t)
	{
		_lock.Enter();
		if (_queue.empty())
		{
			_lock.Leave();
			return false;
		}
		t = std::move(_queue.back());
		_lock.Leave();
		return true;
	}
};

template <typename T>
class SafeArray
{
private:
	std::vector<T> _array;
	CriticalSectionLock _lock;

public:
	SafeArray() {}
	SafeArray(SafeArray &&other) { _array = other._array; }
	~SafeArray() {}
	void clear()
	{
		_lock.Enter();
		_array.clear();
		_lock.Leave();
	}
	bool empty()
	{
		_lock.Enter();
		bool result = _array.empty();
		_lock.Leave();
		return result;
	}
	int size()
	{
		_lock.Enter();
		int result = _array.size();
		_lock.Leave();
		return result;
	}
	// 数组添加元素
	void emplace(T &t)
	{
		_lock.Enter();
		_array.emplace_back(t);
		_lock.Leave();
	}
	// 数组访问元素
	bool getIndexElement(size_t index, T &t)
	{
		_lock.Enter();
		if (_array.empty() || index >= _array.size())
		{
			_lock.Leave();
			return false;
		}
		t = std::move(_array.at(index));
		_lock.Leave();
		return true;
	}
	// 数组删除元素
	bool deleteIndexElement(size_t index)
	{
		_lock.Enter();
		if (_array.empty() || index >= _array.size())
		{
			_lock.Leave();
			return false;
		}
		_lock.Leave();
		_array.erase(_array.begin() + index);
		return true;
	}

	void EnsureCall(std::function<void(std::vector<T> &array)> callback)
	{
		if (callback)
		{
			_lock.Enter();
			callback(this->_array);
			_lock.Leave();
		}
	}
};
