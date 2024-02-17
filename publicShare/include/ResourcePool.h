#pragma once

#ifdef __linux__
#include <mutex>
#elif _WIN32
#include <synchapi.h>
#endif

#include <list>
#include <vector>

template <class DataType>
class ResPool
{
protected:
	std::list<DataType *> _iDleList;
#ifdef _WIN32
	CRITICAL_SECTION _csLock;
#define GETRESLOCK EnterCriticalSection(&_csLock)
#define GETRESUNLOCK LeaveCriticalSection(&_csLock)
#elif __linux__
	std::mutex _mutex;
#define GETRESLOCK std::lock_guard<std::mutex> lock(_mutex);
#define GETRESUNLOCK
#endif
	std::vector<DataType *> _datas;
	int maxResNum = 1000;

public:
	ResPool(int InitResNum = 300, int maxResNum = 1000)
	{
		maxResNum = std::max(InitResNum, maxResNum);
#ifdef _WIN32
		InitializeCriticalSection(&_csLock);
#endif
		_iDleList.clear();
		_datas.clear();

		GETRESLOCK;
		for (size_t i = 0; i < InitResNum; i++)
		{
			DataType *data = new DataType();
			_iDleList.push_back(data);
			_datas.push_back(data);
		}
		GETRESUNLOCK;
	}

	~ResPool()
	{
		GETRESLOCK;
		for (auto it = _iDleList.begin(); it != _iDleList.end(); it++)
		{
			delete (*it);
		}
		_iDleList.clear();
		GETRESUNLOCK;
#ifdef _WIN32
		DeleteCriticalSection(&_csLock);
#endif
	}

	// 分配空间
	virtual DataType *AllocateData()
	{
		DataType *data = NULL;

		GETRESLOCK;
		if (_iDleList.size() > 0) // list不为空，从list中取一个
		{
			data = _iDleList.back();
			_iDleList.pop_back();
		}
		else // list为空，新建一个
		{
			data = new DataType();
			_datas.push_back(data);
		}
		GETRESUNLOCK;

		return data;
	}

	// 回收
	void ReleaseData(DataType *data)
	{
		GETRESLOCK;
		if (_datas.size() < maxResNum)
		{
			ResetData(data);
			_iDleList.push_front(data);
		}
		else
		{
			auto it = find(_datas.begin(), _datas.end(), data);
			_datas.erase(it);
			SAFE_DELETE(data);
		}
		GETRESUNLOCK;
	}

	virtual void ResetData(DataType *data) {}
};