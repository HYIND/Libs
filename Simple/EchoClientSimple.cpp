#include "Session/CustomTcpSession.h"
#include "Session/CustomWebSocketSession.h"
#include "Core/NetCore.h"
#include "fmt/core.h"
#include <iostream>
#include <random>
#include <chrono>

using namespace std;

void signal_handler(int sig)
{
    if (sig == SIGINT)
    {
        StopNetCoreLoop();
        exit(0);
    }
}

class EchoClient
{
public:
    EchoClient()
    {
        session = std::make_unique<CustomTcpSession>();
    }
    ~EchoClient()
    {
    }

public:
    void PrintMessage(BaseNetWorkSession *basesession, Buffer *recv)
    {
        cout << fmt::format("Client RecvData: RemoteIpAddr={}:{}, data:{} \n",
                            basesession->GetIPAddr(), basesession->GetPort(), std::string(recv->Byte(), recv->Length()));
    }

    void PrintRequestMessage(BaseNetWorkSession *basesession, Buffer *recv, Buffer *resp)
    {
        cout << fmt::format("Client RecvRequest: RemoteIpAddr={}:{}, data:{} \n",
                            basesession->GetIPAddr(), basesession->GetPort(), std::string(recv->Byte(), recv->Length()));
        resp->CopyFromBuf(*recv);
    }

    void PrintCloseConnect(BaseNetWorkSession *session)
    {
        cout << fmt::format("Client Connection Close: RemoteIpAddr={}:{} \n",
                            session->GetIPAddr(), session->GetPort());
    }

    bool ConnectTo(const std::string &IP, uint16_t Port)
    {
        {
            // async
            isconnected = session->ConnectAsync(IP, Port).sync_wait();
        }
        {
            // sync
            // result = session->Connect(IP, Port);
        }

        if (isconnected)
        {
            session->BindRecvDataCallBack(std::bind(&EchoClient::PrintMessage, this, std::placeholders::_1, std::placeholders::_2));
            session->BindRecvRequestCallBack(std::bind(&EchoClient::PrintRequestMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
            session->BindSessionCloseCallBack(std::bind(&EchoClient::PrintCloseConnect, this, std::placeholders::_1));
        }
        return isconnected;
    }

    bool SendMsg(const std::string &msg)
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
    std::cout << "NetWork test start...\n";

    InitNetCore();
    RunNetCoreLoop();

    std::string IP = "192.168.58.130";
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

int main(int argc, char *argv[])
{

    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);

    testNet();
}