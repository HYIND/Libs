#include "Core/BaseSocket.h"

int CloseSocket(BaseSocket socket)
{
#ifdef __linux__
	return close(socket);
#elif _WIN32
	return closesocket(socket);
#endif
}