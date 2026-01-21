#include "Session/CustomWebSocketSession.h"
#include "Session/CustomTcpSession.h"
#include "Session/SessionListener.h"
#include "Core/NetCore.h"
#include "fmt/core.h"
#include <iostream>
#include <atomic>

using namespace std;

void signal_handler(int sig)
{
    if (sig == SIGINT)
    {
        StopNetCoreLoop();
    }
}

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
            [&](std::vector<BaseNetWorkSession *> &array) -> void
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
    void PrintSessionEstablish(BaseNetWorkSession *session)
    {
        cout << fmt::format("SessionEstablish:RemoteAddr={}:{} \n",
                            session->GetIPAddr(), session->GetPort());
        session->BindRecvDataCallBack(std::bind(&EchoServer::EchoMessage, this, placeholders::_1, placeholders::_2));
        session->BindSessionCloseCallBack(std::bind(&EchoServer::PrintCloseConnect, this, placeholders::_1));

        if (CustomTcpSession *customtcpsession = dynamic_cast<CustomTcpSession *>(session))
        {
            customtcpsession->BindRecvRequestCallBack(std::bind(&EchoServer::EchoRequestMessage, this, placeholders::_1, placeholders::_2, placeholders::_3));
        }

        sessions.emplace(session);
    }

    void EchoMessage(BaseNetWorkSession *basesession, Buffer *recv)
    {
        CustomTcpSession *session = (CustomTcpSession *)basesession;

        cout << fmt::format("EchoServer recvData:RemoteAddr={}:{}, data:{} \n",
                            session->GetIPAddr(), session->GetPort(), std::string(recv->Byte(), recv->Length()));

        // Echo
        session->AsyncSend(*recv);
    }

    void EchoRequestMessage(BaseNetWorkSession *basesession, Buffer *recv, Buffer *resp)
    {
        CustomTcpSession *session = (CustomTcpSession *)basesession;

        cout << fmt::format("EchoServer recvData:RemoteAddr={}:{}, data:{} \n",
                            session->GetIPAddr(), session->GetPort(), std::string(recv->Byte(), recv->Length()));

        // Echo
        resp->CopyFromBuf(*recv);
        return;
    }

    void PrintCloseConnect(BaseNetWorkSession *session)
    {

        cout << fmt::format("EchoServer ClientConn Close: RemoteIpAddr={}:{} \n",
                            session->GetIPAddr(), session->GetPort());

        sessions.EnsureCall(
            [&](std::vector<BaseNetWorkSession *> &array) -> void
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

    bool Listen(const std::string &IP, int Port)
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
    SafeArray<BaseNetWorkSession *> sessions;
};

int main()
{
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    InitNetCore();

    std::string IP = "192.168.58.130";
    int Port = 5555;

    EchoServer server;
    if (!server.Listen(IP, Port))
    {
        std::cout << "EchoServer Listen [" << IP << ":" << Port << "] fail!\n";
        return -1;
    }
    std::cout << "EchoServer Listen [" << IP << ":" << Port << "] success!\n";

    RunNetCoreLoop(true);
}
