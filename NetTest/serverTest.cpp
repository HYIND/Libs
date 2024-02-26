#include "Session/CustomWebSocketSession.h"
#include "Session/CustomTcpSession.h"
#include "Session/SessionListener.h"
#include "Core/NetCore.h"
#include "fmt/core.h"
#include "Log.h"
#include <iostream>

using namespace std;

class Manager
{
public:
    void callBackSessionEstablish(BaseNetWorkSession *session)
    {
        session->BindRecvDataCallBack(std::bind(&Manager::PrintMessage, this, placeholders::_1, placeholders::_2, placeholders::_3));
        session->BindSessionCloseCallBack(std::bind(&Manager::PrintCloseConnect, this, placeholders::_1));
        sessions.emplace(session);
    }

    void PrintMessage(BaseNetWorkSession *basesession, Buffer *recv, Buffer *response)
    {
        CustomTcpSession *session = (CustomTcpSession *)basesession;

        cout << fmt::format("Server recvData:RemoteAddr={}:{}, data:{} \n",
                            session->GetIPAddr(), session->GetPort(), recv->Byte());

        if (strncmp(recv->Byte(), "AwaitRequest", 12) == 0)
        {
            Buffer buf("ServerAwaitResponse!", 20);
            response->Write(buf);
        }
        else
        {
            session->AsyncSend(Buffer("ServerAsyncResponse"));
        }
    }

    void PrintCloseConnect(BaseNetWorkSession *session)
    {

        cout << fmt::format("Client Connection Close: RemoteIpAddr={}:{} \n",
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

        SAFE_DELETE(session);
    }

private:
    SafeArray<BaseNetWorkSession *> sessions;
};

int main()
{
    LOGGER->SetLoggerPath("server.log");
    InitNetCore();

    Manager a;
    NetWorkSessionListener listener(SessionType::CustomWebSockectSession);
    listener.BindSessionEstablishCallBack(std::bind(&Manager::callBackSessionEstablish, &a, placeholders::_1));
    if (!listener.Listen("127.0.0.1", 8888))
    {
        perror("listen error !");
        return -1;
    }

    RunNetCoreLoop(true);
}
