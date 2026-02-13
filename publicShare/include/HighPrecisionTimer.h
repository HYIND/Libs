#pragma once

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <mmiscapi.h>
#include <iostream>
#include <process.h>
#include <list>
#include <atomic>

#pragma comment(lib, "winmm.lib")

class HighPrecisionTimer
{
private:
	// 定时器请求结构
	struct TimerRequest
	{
		DWORD expireTime; // 到期时间（timeGetTime值）
		DWORD duration;	  // 延时长度
		HANDLE hEvent;	  // 通知事件
		BOOL bSignaled;	  // 是否已触发
	};

	// 全局状态
	static LARGE_INTEGER sm_liPerfFreq;
	static UINT sm_wAccuracy;
	static BOOL sm_bInitialized;
	static MMRESULT sm_mmTimerId;
	static CRITICAL_SECTION sm_cs;
	static std::list<TimerRequest *> sm_requests;
	static std::atomic<bool> sm_csInitialized;

	// 私有构造函数，禁止实例化
	HighPrecisionTimer() = delete;
	~HighPrecisionTimer() = delete;
	HighPrecisionTimer(const HighPrecisionTimer &) = delete;
	HighPrecisionTimer &operator=(const HighPrecisionTimer &) = delete;

	// RAII临界区锁
	class CriticalSectionLock
	{
	public:
		CriticalSectionLock();
		~CriticalSectionLock();
	};

	// 初始化临界区（线程安全，只初始化一次）
	static void InitializeCriticalSectionOnce();

	// 多媒体定时器回调
	static void CALLBACK TimerProc(UINT, UINT, DWORD_PTR, DWORD_PTR, DWORD_PTR);

public:
	static BOOL Initialize();	// 初始化定时器系统
	static void Uninitialize(); // 释放资源

	// 多媒体定时器版睡眠 - 1ms精度，CPU占用低
	static BOOL MSleep(DWORD milliseconds);

	// 检查系统是否已初始化
	static BOOL IsInitialized();
};

#elif __linux__
#include<cstdint> 
class HighPrecisionTimer
{
public:
	static bool Initialize();	// 初始化定时器系统
	static void Uninitialize(); // 释放资源

	static bool MSleep(uint32_t milliseconds);

	static bool IsInitialized();
};

#endif