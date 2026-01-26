#include "Session/CustomWebSocketSession.h"
#include "Session/CustomTcpSession.h"
#include "Session/SessionListener.h"
#include "Core/NetCore.h"
#include <iostream>
#include <atomic>

using namespace std;

#ifdef _WIN32
BOOL WINAPI CtrlHandler(DWORD dwCtrlType) {
	switch (dwCtrlType) {
	case CTRL_C_EVENT:
	case CTRL_BREAK_EVENT:
	case CTRL_CLOSE_EVENT:
		StopNetCoreLoop();
		ExitProcess(0);
		return TRUE;

	default:
		return FALSE;
	}
}
#elif __linux__
void signal_handler(int sig)
{
	if (sig == SIGINT)
	{
		StopNetCoreLoop();
		exit(0);
	}
}
#endif

class EchoServer
{
public:
	EchoServer()
	{
		listener = std::make_unique<NetWorkSessionListener>(SessionType::CustomTCPSession);
		listener->BindSessionEstablishCallBack(std::bind(&EchoServer::PrintSessionEstablish, this, placeholders::_1));
	}
	~EchoServer()
	{
		sessions.EnsureCall(
			[&](std::vector<BaseNetWorkSession*>& array) -> void
			{
				for (auto it = array.begin(); it != array.end(); it++)
				{
					auto session = *it;
					array.erase(it);
					DeleteLater(session);
				}
			});
	}

public:
	void PrintSessionEstablish(BaseNetWorkSession* session)
	{
		cout << "SessionEstablish:RemoteAddr=" << session->GetIPAddr()
			<< ":" << session->GetPort() << " \n";

		session->BindRecvDataCallBack(std::bind(&EchoServer::EchoMessage, this, placeholders::_1, placeholders::_2));
		session->BindSessionCloseCallBack(std::bind(&EchoServer::PrintCloseConnect, this, placeholders::_1));

		if (CustomTcpSession* customtcpsession = dynamic_cast<CustomTcpSession*>(session))
		{
			customtcpsession->BindRecvRequestCallBack(std::bind(&EchoServer::EchoRequestMessage, this, placeholders::_1, placeholders::_2, placeholders::_3));
		}

		sessions.emplace(session);
	}

	void EchoMessage(BaseNetWorkSession* basesession, Buffer* recv)
	{
		CustomTcpSession* session = (CustomTcpSession*)basesession;

		string data(recv->Byte(), recv->Length());
		cout << "EchoServer recvData:RemoteAddr=" << session->GetIPAddr()
			<< ":" << session->GetPort()
			<< ", data:" << data << " \n";

		// Echo
		session->AsyncSend(*recv);
	}

	void EchoRequestMessage(BaseNetWorkSession* basesession, Buffer* recv, Buffer* resp)
	{
		CustomTcpSession* session = (CustomTcpSession*)basesession;

		string data(recv->Byte(), recv->Length());

		cout << "EchoServer recvData:RemoteAddr=" << session->GetIPAddr()
			<< ":" << session->GetPort()
			<< ", data:" << data << " \n";

		// Echo
		resp->CopyFromBuf(*recv);
		return;
	}

	void PrintCloseConnect(BaseNetWorkSession* session)
	{

		cout << "EchoServer ClientConn Close: RemoteIpAddr=" << session->GetIPAddr()
			<< ":" << session->GetPort() << " \n";

		sessions.EnsureCall(
			[&](std::vector<BaseNetWorkSession*>& array) -> void
			{
				for (auto it = array.begin(); it != array.end(); it++)
				{
					if ((*it) == session)
					{
						array.erase(it);
						return;
					}
				}
			}

		);

		DeleteLater(session);
	}

	bool Listen(const std::string& IP, int Port)
	{
		if (!listener->Listen(IP, Port))
		{
			perror("listen error !");
			return false;
		}
		return true;
	}

private:
	std::unique_ptr<NetWorkSessionListener> listener;
	SafeArray<BaseNetWorkSession*> sessions;
};

int main()
{
#ifdef _WIN32
	if (!SetConsoleCtrlHandler(CtrlHandler, TRUE)) {
		printf("Failed to set control handler\n");
		return 1;
	}
#elif __linux__
	struct sigaction sa;
	sa.sa_handler = signal_handler;
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
#endif

	InitNetCore();

	std::string IP = "127.0.0.1";
	int Port = 5555;

	EchoServer server;
	if (!server.Listen(IP, Port))
	{
		std::cout << "EchoServer Listen [" << IP << ":" << Port << "] fail!\n";
		system("pause");
		return -1;
	}
	std::cout << "EchoServer Listen [" << IP << ":" << Port << "] success!\n";

	RunNetCoreLoop(true);
}
