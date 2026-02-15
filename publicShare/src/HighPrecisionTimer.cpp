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

	EnterCriticalSection(&sm_cs);

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
	LeaveCriticalSection(&sm_cs);
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
		EnterCriticalSection(&sm_cs);
		for (auto req : sm_requests)
		{
			if (req->hEvent)
				SetEvent(req->hEvent);
			delete req;
		}
		sm_requests.clear();
		LeaveCriticalSection(&sm_cs);
		DeleteCriticalSection(&sm_cs);
		sm_csInitialized = false;
	}
}

BOOL HighPrecisionTimer::MSleep(DWORD milliseconds)
{
	if (!sm_bInitialized)
		return FALSE;

	if (milliseconds == 0)
		return TRUE;

	HANDLE hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (!hEvent)
		return FALSE;

	BOOL result = FALSE;

	TimerRequest *req = new TimerRequest;
	req->expireTime = timeGetTime() + milliseconds;
	req->duration = milliseconds;
	req->hEvent = hEvent;
	req->bSignaled = FALSE;

	{
		EnterCriticalSection(&sm_cs);
		sm_requests.push_back(req);
		LeaveCriticalSection(&sm_cs);
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
		EnterCriticalSection(&sm_cs);
		sm_requests.remove(req);
		delete req;
		req = nullptr;
		result = FALSE;
		LeaveCriticalSection(&sm_cs);
	}

	CloseHandle(hEvent);
	return result;
}

void HighPrecisionTimer::USleepBusy(DWORD microseconds)
{
	LARGE_INTEGER freq, start, current;
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&start);

	LONGLONG target = (LONGLONG)((double)microseconds * freq.QuadPart / 1000000.0);

	do
	{
		QueryPerformanceCounter(&current);
	} while ((current.QuadPart - start.QuadPart) < target);
}

void HighPrecisionTimer::USleep(DWORD microseconds)
{
	if (microseconds <= 0)
		return;

	if (microseconds >= 2000) // 2ms以上用多媒体定时器
	{
		MSleep(microseconds / 1000);

		// 补偿余数
		DWORD remainder = microseconds % 1000;
		if (remainder > 0)
			USleepBusy(remainder);
	}
	else // 2ms以下直接忙等待
	{
		USleepBusy(microseconds);
	}
}

void HighPrecisionTimer::SpinWait(DWORD microseconds)
{
	USleepBusy(microseconds);
}

// 检查系统是否已初始化
BOOL HighPrecisionTimer::IsInitialized()
{
	return sm_bInitialized;
}

#elif __linux__

#include <unistd.h>
#include <sys/timerfd.h>
#include <iostream>

bool timeFdSleep(uint32_t milliseconds)
{
	int timerFd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
	if (timerFd == -1)
		return false;

	struct itimerspec spec = {0};
	spec.it_value.tv_sec = milliseconds / 1000;
	spec.it_value.tv_nsec = (milliseconds % 1000) * 1000000;

	if (timerfd_settime(timerFd, 0, &spec, nullptr) != 0)
	{
		if (timerFd != -1)
			close(timerFd);
		return false;
	}

	uint64_t expirations = 0;
	ssize_t s = read(timerFd, &expirations, sizeof(uint64_t));
	if (s != sizeof(uint64_t))
	{
		if (timerFd != -1)
			close(timerFd);
		return false;
	}
	if (timerFd != -1)
		close(timerFd);
	return true;
}

bool HighPrecisionTimer::Initialize()
{
	return true;
}
void HighPrecisionTimer::Uninitialize()
{
}

bool HighPrecisionTimer::MSleep(uint32_t milliseconds)
{

	if (timeFdSleep(milliseconds))
		return true;

	usleep(1000 * milliseconds);
	return true;
}

bool HighPrecisionTimer::IsInitialized()
{
	return true;
}

#endif