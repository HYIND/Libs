#include "Session/CustomTcpSession.h"
#include "Session/CustomWebSocketSession.h"
#include "Core/NetCore.h"
#include <iostream>
#include <random>
#include <chrono>

using namespace std;

#ifdef max
#undef max
#endif

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

class EchoClient
{
public:
	EchoClient()
	{
		session = std::make_unique<CustomTcpSession>();
	}
	~EchoClient()
	{
		session->Release();
	}

public:
	void PrintMessage(BaseNetWorkSession* basesession, Buffer* recv)
	{
		string data(recv->Byte(), recv->Length());
		cout << "Client RecvData: RemoteIpAddr=" << basesession->GetIPAddr()
			<< ":" << basesession->GetPort()
			<< ", data:" << data << " \n";
	}

	void PrintRequestMessage(BaseNetWorkSession* basesession, Buffer* recv, Buffer* resp)
	{

		string data(recv->Byte(), recv->Length());
		cout << "Client RecvRequest: RemoteIpAddr=" << basesession->GetIPAddr()
			<< ":" << basesession->GetPort()
			<< ", data:" << data << " \n";
		resp->CopyFromBuf(*recv);
	}

	void PrintCloseConnect(BaseNetWorkSession* session)
	{
		cout << "Client Connection Close: RemoteIpAddr=" << session->GetIPAddr()
			<< ":" << session->GetPort() << " \n";
	}

	bool ConnectTo(const std::string& IP, uint16_t Port)
	{
		{
			// async
			auto task = session->Connect(IP, Port);
			isconnected = task.sync_wait();
		}

		if (isconnected)
		{
			session->BindRecvDataCallBack(std::bind(&EchoClient::PrintMessage, this, std::placeholders::_1, std::placeholders::_2));
			session->BindRecvRequestCallBack(std::bind(&EchoClient::PrintRequestMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
			session->BindSessionCloseCallBack(std::bind(&EchoClient::PrintCloseConnect, this, std::placeholders::_1));
		}
		return isconnected;
	}

	bool SendMsg(const std::string& msg)
	{
		return session->AsyncSend(Buffer(msg));
	}

	bool isConnect()
	{
		return isconnected;
	}

public:
	std::unique_ptr<CustomTcpSession> session;
	bool isconnected = false;
};

void testNet()
{
	std::string IP = "127.0.0.1";
	int port = 5555;

	EchoClient client;
	if (!client.ConnectTo(IP, port))
	{
		std::cout << "EchoClient ConnectTo [" << IP << ":" << port << "] fail!\n";
		return;
	}
	std::cout << "EchoClient ConnectTo [" << IP << ":" << port << "] success!\n";

	std::cout << "Enter Message To Server ('stop' or 'exit' or 'quit' if you want to exit)\n";
	bool stop_requested = false;
	while (!stop_requested || !client.isConnect())
	{
		std::string input;
		if (std::cin.peek() != EOF)
		{
			if (!std::getline(std::cin, input))
			{
				if (std::cin.eof())
				{
					break;
				}
				std::cin.clear();
				std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
				continue;
			}

			if (input == "stop" || input == "exit" || input == "quit")
			{
				break;
			}
			if (!input.empty())
				client.SendMsg(input);
		}
	}

	std::cout << "NetWork test end...\n";
}

int main(int argc, char* argv[])
{
	system("chcp 65001 > nul"); // 切换到 UTF-8

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

	std::cout << "NetWork test start...\n";

	InitNetCore();
	RunNetCoreLoop();

	testNet();

	std::cout << "按回车结束程序..." << std::endl;
	std::cin.get();
}
