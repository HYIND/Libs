#pragma once

#include <map>
#include <queue>
#include <mutex>
#include <functional>
#include <iostream>

template <typename K, typename V>
class SafeMap
{
public:
	SafeMap() {}

	~SafeMap() {}

	SafeMap(const SafeMap& rhs)
	{
		map_ = rhs.map_;
	}

	SafeMap& operator=(const SafeMap& rhs)
	{
		if (&rhs != this)
		{
			map_ = rhs.map_;
		}

		return *this;
	}

	V& operator[](const K& key)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		return map_[key];
	}

	int Size()
	{
		std::lock_guard<std::mutex> lock(mutex_);
		return map_.size();
	}

	bool IsEmpty()
	{
		std::lock_guard<std::mutex> lock(mutex_);
		return map_.empty();
	}

	bool Insert(const K& key, const V& value)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		auto ret = map_.insert(std::pair<K, V>(key, value));
		return ret.second;
	}

	void EnsureInsert(const K& key, const V& value)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		auto ret = map_.insert(std::pair<K, V>(key, value));
		// find key and cannot insert
		if (!ret.second)
		{
			map_.erase(ret.first);
			map_.insert(std::pair<K, V>(key, value));
			return;
		}
		return;
	}

	bool Find(const K& key, V& value)
	{
		bool ret = false;
		std::lock_guard<std::mutex> lock(mutex_);

		auto iter = map_.find(key);
		if (iter != map_.end())
		{
			value = iter->second;
			ret = true;
		}

		return ret;
	}

	bool FindOldAndSetNew(const K& key, V& oldValue, const V& newValue)
	{
		bool ret = false;
		std::lock_guard<std::mutex> lock(mutex_);

		if (map_.size() > 0)
		{
			auto iter = map_.find(key);
			if (iter != map_.end())
			{
				oldValue = iter->second;
				map_.erase(iter);
				map_.insert(std::pair<K, V>(key, newValue));
				ret = true;
			}
		}

		return ret;
	}

	void Erase(const K& key)
	{
		std::lock_guard<std::mutex> lock(mutex_);

		map_.erase(key);
	}

	void Clear()
	{
		std::lock_guard<std::mutex> lock(mutex_);

		map_.clear();
		return;
	}

	void EnsureCall(std::function<void(std::map<K, V>& map)> callback)
	{
		if (callback)
		{
			std::lock_guard<std::mutex> lock(mutex_);
			callback(this->map_);
		}
	}

private:
	std::mutex mutex_;
	std::map<K, V> map_;
};

template <typename T>
class SafeQueue
{
private:
	std::queue<T> _queue;
	std::mutex _mutex;

public:
	SafeQueue() {}
	SafeQueue(SafeQueue&& other) {}
	~SafeQueue() {}
	bool empty()
	{
		std::unique_lock<std::mutex> lock(_mutex);
		return _queue.empty();
	}
	int size()
	{
		std::unique_lock<std::mutex> lock(_mutex);
		return _queue.size();
	}
	// 队列添加元素
	void enqueue(T& t)
	{
		std::unique_lock<std::mutex> lock(_mutex);
		_queue.emplace(t);
	}
	// 队列取出元素
	bool dequeue(T& t)
	{
		std::unique_lock<std::mutex> lock(_mutex);
		if (_queue.empty())
			return false;
		t = std::move(_queue.front());
		_queue.pop();
		return true;
	}
	// 查看队列首元素
	bool front(T& t)
	{
		std::unique_lock<std::mutex> lock(_mutex);
		if (_queue.empty())
			return false;
		t = std::move(_queue.front());
		return true;
	}
	// 查看队列尾元素
	bool back(T& t)
	{
		std::unique_lock<std::mutex> lock(_mutex);
		if (_queue.empty())
			return false;
		t = std::move(_queue.back());
		return true;
	}
};