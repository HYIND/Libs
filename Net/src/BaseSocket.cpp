#include "Core/BaseSocket.h"

bool CloseSocket(BaseSocket socket)
{
	if (socket <= 0)
		return false;
#ifdef __linux__
	return (close(socket) != -1);
#elif _WIN32
	return closesocket(socket) != SOCKET_ERROR;
#endif
}