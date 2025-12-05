#include "Core/NetCoredef.h"

using namespace std;

#if defined(__linux__)

void InitNetCore()
{
}

bool NetCoreRunning()
{
	return NetCore->Running();
}

void DeleteLater(DeleteLaterImpl *ptr)
{
	NetCore->AddPendingDeletion(ptr);
}

#if defined(IO_URING_ON)
#include "Core/IOuringCore.h"
#define NetCore IOuringCoreProcess::Instance()
void RunNetCoreLoop(bool isBlock)
{
	if (!NetCore->Running())
	{
		thread CoreThread(&IOuringCoreProcess::Run, IOuringCoreProcess::Instance());
		if (isBlock)
			CoreThread.join();
		else
			CoreThread.detach();
	}
}
#else
void RunNetCoreLoop(bool isBlock)
{
	if (!NetCoreRunning())
	{
		thread CoreThread(&EpollCoreProcess::Run, EpollCoreProcess::Instance());
		if (isBlock)
			CoreThread.join();
		else
			CoreThread.detach();
	}
}

#endif
#elif define(_WIN32)
#endif
