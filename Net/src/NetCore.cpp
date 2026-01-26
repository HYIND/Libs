#include "Core/NetCoredef.h"

using namespace std;

NET_API void InitNetCore()
{
#ifdef _WIN32
	WSADATA wsa;
	WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
}

NET_API bool NetCoreRunning()
{
	return NetCore->Running();
}

NET_API void DeleteLater(DeleteLaterImpl* ptr)
{
	NetCore->AddPendingDeletion(ptr);
}

NET_API void StopNetCoreLoop()
{
	if (NetCoreRunning())
	{
		NetCore->Stop();
	}
}

#ifdef __linux__
#if defined(IO_URING_ON)
#include "Core/IOuringCore.h"
#define NetCore IOuringCoreProcess::Instance()
NET_API void RunNetCoreLoop(bool isBlock)
{
	if (!NetCore->Running())
	{
		thread CoreThread(&IOuringCoreProcess::Run, IOuringCoreProcess::Instance());
		if (isBlock)
			CoreThread.join();
		else
		{
			this_thread::sleep_for(std::chrono::milliseconds(500));
			CoreThread.detach();
		}
	}
}
#else
NET_API void RunNetCoreLoop(bool isBlock)
{
	if (!NetCoreRunning())
	{
		thread CoreThread(&EpollCoreProcess::Run, EpollCoreProcess::Instance());
		if (isBlock)
			CoreThread.join();
		else
		{
			this_thread::sleep_for(std::chrono::milliseconds(500));
			CoreThread.detach();
		}
	}
}
#endif
#elif _WIN32
NET_API void RunNetCoreLoop(bool isBlock)
{
	if (!NetCoreRunning())
	{
		thread CoreThread(&IOCPCoreProcess::Run, IOCPCoreProcess::Instance());
		if (isBlock)
			CoreThread.join();
		else
		{
			this_thread::sleep_for(std::chrono::milliseconds(500));
			CoreThread.detach();
		}
	}
}
#endif
