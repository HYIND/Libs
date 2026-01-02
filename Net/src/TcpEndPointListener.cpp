#include "EndPoint/TcpEndPointListener.h"
#include "EndPoint/TCPEndPoint.h"
#include "EndPoint/PureTCPClient.h"
#include "EndPoint/WebSocketClient.h"
#include "Timer.h"

static int64_t GetTimestampMilliseconds()
{
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

constexpr int64_t connection_expired_time_ms = 30 * 1000;

struct ClientData
{
    TCPEndPoint *client;
    int64_t expiredtime;
    ClientData(TCPEndPoint *client) : client(client)
    {
        expiredtime = GetTimestampMilliseconds() + connection_expired_time_ms;
    }
};

TcpEndPointListener::TcpEndPointListener(TCPNetProtocol proto) : _Protocol(proto)
{
    BaseListener = std::make_shared<TCPTransportListener>();
    BaseListener->BindAcceptCallBack(std::bind(&TcpEndPointListener::RecvCon, this, std::placeholders::_1));

    // 定期检测过期连接
    CleanExpiredTask = TimerTask::CreateRepeat("TcpEndPointListener::CleanExpiredClient",
                                               30 * 1000,
                                               std::bind(&TcpEndPointListener::CleanExpiredClient, this),
                                               30 * 1000);
    CleanExpiredTask->Run();
}

TcpEndPointListener::~TcpEndPointListener()
{
    if (CleanExpiredTask)
    {
        CleanExpiredTask->Clean();
        CleanExpiredTask = nullptr;
    }
}

TCPNetProtocol TcpEndPointListener::Protocol()
{
    return _Protocol;
}

void TcpEndPointListener::SetProtocol(const TCPNetProtocol &proto)
{
    _Protocol = proto;
}

bool TcpEndPointListener::Listen(const std::string &IP, int Port)
{
    return BaseListener->Listen(IP, Port);
}

void TcpEndPointListener::BindEstablishConnectionCallBack(std::function<void(TCPEndPoint *)> callback)
{
    _callBackEstablish = callback;
}

void TcpEndPointListener::RecvCon(std::shared_ptr<TCPTransportConnection> waitCon)
{
    switch (_Protocol)
    {
    case TCPNetProtocol::PureTCP:
    {

        TCPEndPoint *client = new PureTCPClient(waitCon);
        waitClients.emplace(std::make_shared<ClientData>(client));
        waitCon->BindBufferCallBack(std::bind(&TcpEndPointListener::Handshake, this, std::placeholders::_1, std::placeholders::_2));
        waitCon->BindRDHUPCallBack(std::bind(&TcpEndPointListener::ConClose, this, std::placeholders::_1));
        break;
    }
    case TCPNetProtocol::WebSocket:
    {
        TCPEndPoint *client = new WebSocketClient(waitCon);
        waitClients.emplace(std::make_shared<ClientData>(client));
        waitCon->BindBufferCallBack(std::bind(&TcpEndPointListener::Handshake, this, std::placeholders::_1, std::placeholders::_2));
        waitCon->BindRDHUPCallBack(std::bind(&TcpEndPointListener::ConClose, this, std::placeholders::_1));
        break;
    }
    default:
        std::cout << "TcpEndPointListener::RecvCon ,Protocol Error!\n";
        break;
    }
}

void TcpEndPointListener::ConClose(TCPTransportConnection *Con)
{
    waitClients.EnsureCall(
        [&](std::vector<std::shared_ptr<ClientData>> &array) -> void
        {
            for (auto it = array.begin(); it != array.end();)
            {
                std::shared_ptr<ClientData> clientdata = *it;
                TCPEndPoint *client = clientdata->client;
                if (client->GetBaseCon().get() != Con)
                {
                    it++;
                    continue;
                }
                client->Release();
                array.erase(it);
                DeleteLater(client);
            }
        });
}

void TcpEndPointListener::Handshake(TCPTransportConnection *waitCon, Buffer *buf)
{
    waitClients.EnsureCall(
        [&](std::vector<std::shared_ptr<ClientData>> &array) -> void
        {
            for (auto it = array.begin(); it != array.end();)
            {
                std::shared_ptr<ClientData> clientdata = *it;
                TCPEndPoint *client = clientdata->client;
                if (client->GetBaseCon().get() != waitCon)
                {
                    it++;
                    continue;
                }

                CheckHandshakeStatus result = client->CheckHandshakeTryMsg(*buf); // 由Listener负责调用具体的Client握手方法，并监听请结果
                if (result == CheckHandshakeStatus::Success)
                {
                    if (_callBackEstablish)
                        _callBackEstablish(client);
                    array.erase(it);
                    waitCon->BindBufferCallBack(std::bind(&TCPEndPoint::RecvBuffer, client, std::placeholders::_1, std::placeholders::_2));
                    waitCon->BindRDHUPCallBack(std::bind(&TCPEndPoint::ConnectClose, client, std::placeholders::_1));
                    if (buf->Remain() > 0)
                        client->RecvBuffer(waitCon, buf);
                }
                if (result == CheckHandshakeStatus::BufferAgain)
                    return;
                if (result == CheckHandshakeStatus::Fail)
                {
                    client->Release();
                    array.erase(it);
                    DeleteLater(client);
                }
                return;
            }
        });
}

void TcpEndPointListener::CleanExpiredClient()
{
    int64_t timestamp = GetTimestampMilliseconds();
    waitClients.EnsureCall(
        [&](std::vector<std::shared_ptr<ClientData>> &array) -> void
        {
            for (auto it = array.begin(); it != array.end();)
            {
                std::shared_ptr<ClientData> clientdata = *it;
                TCPEndPoint *client = clientdata->client;
                if (timestamp > clientdata->expiredtime)
                {
                    client->Release();
                    it = array.erase(it);
                    DeleteLater(client);
                }
                else
                {
                    it++;
                }
            }
        });
}