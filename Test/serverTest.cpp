#include "ProtocolEndPoint/CustomTcpProtocolClient.h"
#include "ProtocolEndPoint/WebSocketProtocolClient.h"
#include "Core/NetCore.h"
#include "fmt/core.h"
#include "Log.h"
#include <iostream>

using namespace std;

void PrintMessage(TCPProtocolClient *client, Buffer *recv, Buffer *AckResponse)
{
    cout << fmt::format("Server recvData: fd={}, RemoteAddr={}:{}, data:{} \n",
                        client->GetBaseCon()->GetFd(), client->GetBaseCon()->GetIPAddr(), client->GetBaseCon()->GetPort(), recv->Byte());

    if (strncmp(recv->Byte(), "AwaitRequest", 12) == 0)
    {
        Buffer Response("ServerAwaitResponse!", 20);
        AckResponse->Write(Response);
    }
    else
    {
        client->AsyncSend(Buffer("ServerAsyncResponse"));
    }
}

void PrintCloseConnect(TCPProtocolClient *client)
{
    cout << fmt::format("client Connection Close: fd={}, RemoteIpAddr={}:{} \n",
                        client->GetBaseCon()->GetFd(), client->GetBaseCon()->GetIPAddr(), client->GetBaseCon()->GetPort());
    delete client;
}

class A
{
public:
    int func(TCPProtocolClient *client)
    {
        client->BindMessageCallBack(bind(&PrintMessage, placeholders::_1, placeholders::_2, placeholders::_3));
        client->BindCloseCallBack(bind(&PrintCloseConnect, placeholders::_1));
        // clients.push_back((WebSocketClient *)client);
        return 0;
    }

private:
    vector<WebSocketClient *> clients;
};

int main()
{
    LOGGER->SetLoggerPath("server.log");
    InitNetCore();

    A a;
    TcpProtocolListener listener(CustomProtocol);
    listener.BindEstablishConnectionCallBack(std::bind(&A::func, &a, placeholders::_1));
    if (!listener.Listen("127.0.0.1", 8888))
    {
        perror("listen error !");
        return -1;
    }

    RunNetCoreLoop(true);
}
