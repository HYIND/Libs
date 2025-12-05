/*
	该文件是对IO事件驱动的封装，用于触发网络事件，进行数据的收发
	CoreProcess是网络事件驱动核心
	通过宏（可以在CMakeList中控制）选择io_uring或者epoll封装作为核心
*/

#pragma once

// #define IO_URING_ON

#if defined(__linux__)
#if defined(IO_URING_ON)
#include "Core/IOuringCore.h"
#define NetCore IOuringCoreProcess::Instance()
#else
#include "Core/EpollCore.h"
#define NetCore EpollCoreProcess::Instance()
#endif
#elif define(_WIN32)
#endif
