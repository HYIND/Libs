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

Task<bool> timertask(bool shouwake)
{
    CoTimer timer(std::chrono::milliseconds((int64_t)3000));
    if (shouwake)
        timer.wake();
    CoTimer::WakeType result = co_await timer;
    if (result == CoTimer::WakeType::Error)
    {
        co_return false;
        std::cout << "CoTimer error!\n";
    }
    else if (result == CoTimer::WakeType::TIMEOUT)
    {
        co_return true;
        std::cout << "CoTimer timeout!\n";
    }
    else
    {
        std::cout << "CoTimer manual_wake!\n";
        co_return true;
    }
}

Task<int> testadd(int a, int b)
{
    co_return a + b;
}

Task<void> testCoroutineTask()
{
    { // CoTimer
        std::cout << "CoTimer test start...\n";
        auto task_shoudown = timertask(true);
        auto task = timertask(false);
        bool result_shoudown = co_await task_shoudown;
        bool result = co_await task;
        std::cout << "CoTimer test end...\n";
    }

    { // CoSleep
        std::cout << "CoSleep test start...\n";
        co_await CoSleep(1s);
        std::cout << "CoSleep test end...\n";
    }

    {
        std::cout << "CoroTaskAdd test start...\n";
        std::cout << "CoroTaskAdd input 2+3\n";
        int addresult = co_await testadd(2, 3);
        std::cout << "CoroTaskAdd addresult " << addresult << "\n";
        std::cout << "CoroTaskAdd test end...\n";
    }

    {
        auto NewClientSocket = [](const std::string &IP, uint16_t socket_port, __socket_type protocol, sockaddr_in &sock_addr) -> int
        {
            memset(&sock_addr, '0', sizeof(sock_addr));
            sock_addr.sin_family = AF_INET;
            sock_addr.sin_port = htons(socket_port);

            sock_addr.sin_addr.s_addr = inet_addr(IP.c_str());

            int socket_fd = socket(PF_INET, protocol, 0);

            return socket_fd;
        };

        std::cout << "CoroConnection test start...\n";

        std::string IP = "192.168.58.130";
        int port = 5555;

        sockaddr_in addr;
        int fd = NewClientSocket(IP, port, SOCK_STREAM, addr);
        int result = co_await CoConnection(fd, addr);
        if (fd == -1)
            std::cout << "CoroConnection connect failed!\n";
        else
            std::cout << "CoroConnection connect seccess!\n";
        std::cout << "CoroConnection test end...\n";
    }
}

void testCoroutine()
{
    auto corotask = testCoroutineTask();
    sleep(1);
    bool is_done = corotask.is_done();
    std::cout << "corotask done = [" << is_done << "]\n";
    corotask.sync_wait();
}

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

    testCoroutine();
    testNet();
}