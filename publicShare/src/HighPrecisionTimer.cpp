#include "HighPrecisionTimer.h"

#ifdef _WIN32
// 静态成员定义
LARGE_INTEGER HighPrecisionTimer::sm_liPerfFreq = {};
UINT HighPrecisionTimer::sm_wAccuracy = 0;
BOOL HighPrecisionTimer::sm_bInitialized = FALSE;
MMRESULT HighPrecisionTimer::sm_mmTimerId = 0;
CRITICAL_SECTION HighPrecisionTimer::sm_cs = {};
std::list<HighPrecisionTimer::TimerRequest *> HighPrecisionTimer::sm_requests;
std::atomic<bool> HighPrecisionTimer::sm_csInitialized{false};

HighPrecisionTimer::CriticalSectionLock::CriticalSectionLock()
{
	EnterCriticalSection(&sm_cs);
}

HighPrecisionTimer::CriticalSectionLock::~CriticalSectionLock()
{
	LeaveCriticalSection(&sm_cs);
}

void HighPrecisionTimer::InitializeCriticalSectionOnce()
{
	bool expected = false;
	if (sm_csInitialized.compare_exchange_strong(expected, true))
	{
		InitializeCriticalSection(&sm_cs);
		sm_csInitialized = TRUE;
	}
}

void HighPrecisionTimer::TimerProc(UINT, UINT, DWORD_PTR, DWORD_PTR, DWORD_PTR)
{
	DWORD currentTime = timeGetTime();

	InitializeCriticalSectionOnce();
	CriticalSectionLock lock;

	auto it = sm_requests.begin();
	while (it != sm_requests.end())
	{
		TimerRequest *req = *it;

		if (!req->bSignaled && req->expireTime <= currentTime)
		{
			SetEvent(req->hEvent);
			req->bSignaled = TRUE;
		}

		if (req->bSignaled)
		{
			it = sm_requests.erase(it);
			delete req;
		}
		else
		{
			++it;
		}
	}
}

BOOL HighPrecisionTimer::Initialize()
{
	if (sm_bInitialized)
		return TRUE;

	InitializeCriticalSectionOnce();

	// 获取性能计数器频率
	QueryPerformanceFrequency(&sm_liPerfFreq);

	// 设置多媒体定时器
	TIMECAPS tc;
	if (timeGetDevCaps(&tc, sizeof(TIMECAPS)) == TIMERR_NOERROR)
	{
		sm_wAccuracy = std::min(std::max(tc.wPeriodMin, UINT(1)), tc.wPeriodMax);
		if (timeBeginPeriod(sm_wAccuracy) == TIMERR_NOERROR)
		{
			sm_mmTimerId = timeSetEvent(1, 0, TimerProc, 0, TIME_PERIODIC);
			if (sm_mmTimerId)
			{
				sm_bInitialized = TRUE;
				return sm_bInitialized;
			}
			timeEndPeriod(sm_wAccuracy);
		}
	}
	return sm_bInitialized;
}

void HighPrecisionTimer::Uninitialize()
{
	if (sm_mmTimerId != 0)
	{
		timeKillEvent(sm_mmTimerId);
		sm_mmTimerId = 0;
	}

	if (sm_bInitialized)
	{
		timeEndPeriod(sm_wAccuracy);
		sm_bInitialized = FALSE;
	}

	// 清理所有等待请求
	if (sm_csInitialized)
	{
		CriticalSectionLock lock;
		for (auto req : sm_requests)
		{
			if (req->hEvent)
				SetEvent(req->hEvent);
			delete req;
		}
		sm_requests.clear();
	}
}

BOOL HighPrecisionTimer::MSleep(DWORD milliseconds)
{
	if (!sm_bInitialized && !Initialize())
		return FALSE;

	if (milliseconds == 0)
		return TRUE;

	HANDLE hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (!hEvent)
		return FALSE;

	TimerRequest *req = nullptr;
	BOOL result = FALSE;

	try
	{
		req = new TimerRequest;
		req->expireTime = timeGetTime() + milliseconds;
		req->duration = milliseconds;
		req->hEvent = hEvent;
		req->bSignaled = FALSE;

		{
			CriticalSectionLock lock;
			sm_requests.push_back(req);
		}

		// 等待事件触发或超时
		DWORD waitResult = WaitForSingleObject(hEvent, milliseconds + 100);

		if (waitResult == WAIT_OBJECT_0)
		{
			result = TRUE;
		}
		else
		{
			// 超时或错误，手动清理
			CriticalSectionLock lock;
			sm_requests.remove(req);
			delete req;
			req = nullptr;
			result = FALSE;
		}
	}
	catch (...)
	{
		if (req)
		{
			CriticalSectionLock lock;
			sm_requests.remove(req);
			delete req;
		}
		result = FALSE;
	}

	CloseHandle(hEvent);
	return result;
}

// 检查系统是否已初始化
BOOL HighPrecisionTimer::IsInitialized()
{
	return sm_bInitialized;
}

#elif __linux__

bool HighPrecisionTimer::Initialize()
{
	return false;
}
void HighPrecisionTimer::Uninitialize()
{
}

bool HighPrecisionTimer::MSleep(uint32_t milliseconds)
{
	return false;
}

bool HighPrecisionTimer::IsInitialized()
{
	return false;
}

#endif