#include "Session/CustomTcpSession.h"
#include "Session/CustomWebSocketSession.h"
#include "Core/NetCore.h"
#include "fmt/core.h"
#include "Log.h"
#include <iostream>
#include <random>

using namespace std;

void PrintMessage(BaseNetWorkSession *basesession, Buffer *recv, Buffer *AckResponse)
{
    cout << fmt::format("client recvData: RemoteIpAddr={}:{}, data:{} \n",
                        basesession->GetIPAddr(), basesession->GetPort(), recv->Byte());
}

void PrintCloseConnect(BaseNetWorkSession *session)
{
    cout << fmt::format("client Connection Close: RemoteIpAddr={}:{} \n",
                        session->GetIPAddr(), session->GetPort());
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
    std::map<int, CustomWebSocketSession *> sessions;

    int num = 1000;

    if (argc > 1)
    {
        num = atoi(argv[1]);
        cout << "input threadnum:" << num << "\n";
    }

    auto test = [&](int i, CustomWebSocketSession *session) -> void
    {
        unsigned long count = 0;
        while (!isStop)
        {
            if (count % 10 != 0)
            {
                Buffer buf("332112", 6);
                if (session->AsyncSend(buf))
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
                if (session->AwaitSend(buf, response))
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
        session->Release();
        sessions.erase(i);
        delete session;
        num -= 1;
        return;
    };

    auto testDelay = [&](int i, CustomWebSocketSession *session) -> void
    {
        unsigned long count = 0;
        while (!isStop)
        {
            if (count % 10 != 0)
            {
                Buffer buf("332112", 6);
                if (session->AsyncSend(buf))
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
                if (session->AwaitSend(buf, response))
                {
                    auto end = chrono::steady_clock::now();
                    auto delay = chrono::duration_cast<chrono::milliseconds>(end - start);
                    cout << "AwaitSend delay:" << delay.count() << "ms\n";

                    cout << fmt::format("count={} ,AwaitSendData To {}:{} ,SendData={}  ,And ResponseData={}",
                                        count, session->GetBaseClient()->GetBaseCon()->GetIPAddr(), session->GetBaseClient()->GetBaseCon()->GetPort(), buf.Byte(), response.Byte())
                         << endl;
                }
                else
                {
                    break;
                }
            }

            this_thread::sleep_for(std::chrono::milliseconds(100)); // 睡眠
            count++;
        }
        session->Release();
        sessions.erase(i);
        delete session;
        num -= 1;
        return;
    };

    for (int i = 0; i < num; i++)
    {
        auto session = new CustomWebSocketSession();

        if (!session->Connect("127.0.0.1", 8888))
        {
            perror("connect error !");
            return -1;
        }
        if (i == 0)
            session->BindRecvDataCallBack(bind(&PrintMessage, placeholders::_1, placeholders::_2, placeholders::_3));
        session->BindSessionCloseCallBack(bind(&PrintCloseConnect, placeholders::_1));
        sessions[i] = session;
    }

    for (int i = 0; i < num; i++)
    {
        auto session = sessions[i];
        if ((i + 1) % 50 == 0 || i == num - 1)
        {
            cout << "currentThreadNum:" << i + 1 << "\n";
        }

        if (i == 0)
        {
            thread T(testDelay, i, session);
            T.detach();
        }
        else
        {
            thread T(test, i, session);
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