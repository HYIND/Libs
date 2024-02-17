#include "ProtocolEndPoint/CustomTcpProtocolClient.h"
#include "ProtocolEndPoint/WebSocketProtocolClient.h"
#include "Core/NetCore.h"
#include "fmt/core.h"
#include "Log.h"
#include <iostream>
#include <random>

using namespace std;

void PrintMessage(TCPProtocolClient *client, Buffer *recv, Buffer *AckResponse)
{
    cout << fmt::format("client recvData: fd={}, RemoteIpAddr={}:{}, data:{} \n",
                        client->GetBaseCon()->GetFd(), client->GetBaseCon()->GetIPAddr(), client->GetBaseCon()->GetPort(), (char *)(recv->Data()));
}

void PrintCloseConnect(TCPProtocolClient *client)
{
    cout << fmt::format("client Connection Close: fd={}, RemoteIpAddr={}:{} \n",
                        client->GetBaseCon()->GetFd(), client->GetBaseCon()->GetIPAddr(), client->GetBaseCon()->GetPort());
}

class A
{
public:
    void func()
    {
    }
};

int main(int argc, char *argv[])
{
    LOGGER->SetLoggerPath("client.log");

    InitNetCore();
    RunNetCoreLoop();

    sleep(1);

    bool isStop = false;
    std::map<int, TCPProtocolClient *> clients;

    int num = 10;

    if (argc > 1)
    {
        num = atoi(argv[1]);
        cout << "input threadnum:" << num << "\n";
    }

    auto test = [&](int i, TCPProtocolClient *client) -> void
    {
        unsigned long count = 0;
        while (!isStop)
        {
            if (count % 10 != 0)
            {
                Buffer buf("332112", 6);
                if (client->AsyncSend(buf))
                {
                    // cout << fmt::format("AsyncSendData To {}:{} ,SendData={}",
                    //                     client->GetBaseCon()->GetIPAddr(), client->GetBaseCon()->GetPort(), buf.Byte())
                    //      << endl;
                }
                else
                {
                    break;
                }
            }
            else
            {
                Buffer buf("AwaitRequest", 12), response;
                if (client->AwaitSend(buf, response))
                {
                    // cout << fmt::format("AwaitSendData To {}:{} ,SendData={}  ,And ResponseData={}",
                    //                     client->GetBaseCon()->GetIPAddr(), client->GetBaseCon()->GetPort(), buf.Byte(), response.Byte())
                    //      << endl;
                }
                else
                {
                    break;
                }
            }

            this_thread::sleep_for(std::chrono::milliseconds(100)); // 睡眠
            count++;
        }
        client->Release();
        clients.erase(i);
        delete client;
        num -= 1;
        return;
    };

    auto testDelay = [&](int i, TCPProtocolClient *client) -> void
    {
        unsigned long count = 0;
        while (!isStop)
        {
            if (count % 10 != 0)
            {
                Buffer buf("332112", 6);
                if (client->AsyncSend(buf))
                {
                    // cout << fmt::format("AsyncSendData To {}:{} ,SendData={}",
                    //                     client->GetBaseCon()->GetIPAddr(), client->GetBaseCon()->GetPort(), buf.Byte())
                    //      << endl;
                }
                else
                {
                    break;
                }
            }
            else
            {

                auto start = chrono::steady_clock::now();

                Buffer buf("AwaitRequest", 12), response;
                if (client->AwaitSend(buf, response))
                {
                    cout << fmt::format("count={} ,AwaitSendData To {}:{} ,SendData={}  ,And ResponseData={}",
                                        count, client->GetBaseCon()->GetIPAddr(), client->GetBaseCon()->GetPort(), buf.Byte(), response.Byte())
                         << endl;
                }
                else
                {
                    break;
                }

                auto end = chrono::steady_clock::now();

                auto delay = chrono::duration_cast<chrono::milliseconds>(end - start);
                cout << "AwaitSend delay:" << delay.count() << "ms\n";
            }

            this_thread::sleep_for(std::chrono::milliseconds(100)); // 睡眠
            count++;
        }
        client->Release();
        clients.erase(i);
        delete client;
        num -= 1;
        return;
    };

    for (int i = 0; i < num; i++)
    {
        TCPProtocolClient *client = new CustomTCPProtocolClient();

        // client->BindMessageCallBack(bind(&PrintMessage, placeholders::_1, placeholders::_2, placeholders::_3));
        client->BindCloseCallBack(bind(&PrintCloseConnect, placeholders::_1));
        if (!client->Connet("127.0.0.1", 8888))
        {
            perror("connect error !");
            return -1;
        }
        clients[i] = client;
    }

    for (int i = 0; i < num; i++)
    {
        TCPProtocolClient *client = clients[i];
        if ((i + 1) % 50 == 0 || i == num - 1)
        {
            cout << "currentThreadNum:" << i + 1 << "\n";
        }

        if (i == 0)
        {
            thread T(testDelay, i, client);
            T.detach();
        }
        else
        {
            thread T(test, i, client);
            // this_thread::sleep_for(std::chrono::milliseconds((int)(30000 / float(num)) * i)); // 睡眠2秒
            T.detach();
        }
    }

    while (true)
    {
        this_thread::sleep_for(chrono::seconds(1));
        if (!NetCoreRunning())
            RunNetCoreLoop();
    }
}