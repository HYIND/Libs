#include "Core/BaseSocket.h"

int CloseSocket(BaseSocket socket)
{
#ifdef _linux
	return close(socket);
#elif _WIN32
	return closesocket(socket);
#endif
}