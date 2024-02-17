#include "ProtocolEndPoint/TCPProtocolEndPoint.h"
#include "ProtocolEndPoint/CustomTcpProtocolClient.h"
#include "ProtocolEndPoint/WebSocketProtocolClient.h"

TcpProtocolListener::TcpProtocolListener(TCPNetProtocol proto) : Protocol(proto)
{
    BaseListener.BindAcceptCallBack(std::bind(&TcpProtocolListener::RecvCon, this, std::placeholders::_1));
}
TcpProtocolListener::~TcpProtocolListener()
{
}

bool TcpProtocolListener::Listen(const std::string &IP, int Port)
{
    return BaseListener.Listen(IP, Port);
}

void TcpProtocolListener::BindEstablishConnectionCallBack(std::function<void(TCPProtocolClient *)> callback)
{
    _callBackEstablish = callback;
}

void TcpProtocolListener::RecvCon(TCPNetClient *waitCon)
{
    switch (Protocol)
    {
    case TCPNetProtocol::CustomProtocol:
    {

        TCPProtocolClient *client = new CustomTCPProtocolClient(waitCon);
        waitClients.emplace(client);
        waitCon->BindBufferCallBack(std::bind(&TcpProtocolListener::Handshake, this, std::placeholders::_1, std::placeholders::_2));
        break;
    }
    case TCPNetProtocol::WebSocket:
    {
        TCPProtocolClient *client = new WebSocketClient(waitCon);
        waitClients.emplace(client);
        waitCon->BindBufferCallBack(std::bind(&TcpProtocolListener::Handshake, this, std::placeholders::_1, std::placeholders::_2));
        break;
    }
    default:
        std::cout << "TcpProtocolListener::RecvCon ,Protocol Error!\n";
        break;
    }
}

void TcpProtocolListener::Handshake(TCPNetClient *waitCon, Buffer *buf)
{
    waitClients.EnsureCall(
        [&](std::vector<TCPProtocolClient *> &array) -> void
        {
            for (auto it = array.begin(); it != array.end();)
            {
                TCPProtocolClient *client = *it;
                if (client->GetBaseCon() != waitCon)
                    continue;

                {
                    int result = client->CheckHandshakeTryMsg(*buf); // 由Listener负责调用具体的Client握手方法，并监听请结果
                    if (result == CheckHandshakeStatus::Success)
                    {
                        if (_callBackEstablish)
                            _callBackEstablish(client);
                        array.erase(it);
                        waitCon->BindBufferCallBack(std::bind(&TCPProtocolClient::RecvBuffer, client, std::placeholders::_1, std::placeholders::_2));
                        waitCon->BindRDHUPCallBack(std::bind(&TCPProtocolClient::ConnectClose, client, std::placeholders::_1));
                    }
                    if (result == CheckHandshakeStatus::BufferAgain)
                        return;
                    if (result == CheckHandshakeStatus::Fail)
                    {
                        client->Release();
                        array.erase(it);
                    }
                }
                return;
            }
        });
}

TCPProtocolClient::TCPProtocolClient()
{
}
TCPProtocolClient::~TCPProtocolClient()
{
    if (BaseCon)
        BaseCon->Release();
}

bool TCPProtocolClient::Connet(const std::string &IP, uint16_t Port)
{
    if (!BaseCon->Connet(IP, Port))
        return false;

    BaseCon->BindBufferCallBack(std::bind(&TCPProtocolClient::RecvBuffer, this, std::placeholders::_1, std::placeholders::_2));
    BaseCon->BindRDHUPCallBack(std::bind(&TCPProtocolClient::ConnectClose, this, std::placeholders::_1));

    return true;
}

bool TCPProtocolClient::Release()
{
    if (!BaseCon)
        return true;
    bool result = BaseCon->Release();
    isHandshakeComplete = false;
    _callbackMessage = nullptr;
    _callbackClose = nullptr;
    return result;
}
void TCPProtocolClient::BindMessageCallBack(std::function<void(TCPProtocolClient *, Buffer *, Buffer *AckResponse)> callback)
{
    _callbackMessage = callback;
}
void TCPProtocolClient::BindCloseCallBack(std::function<void(TCPProtocolClient *)> callback)
{
    _callbackClose = callback;
}

bool TCPProtocolClient::RecvBuffer(TCPNetClient *con, Buffer *buffer)
{
    if (con != BaseCon)
        return false;
    return OnRecvBuffer(buffer);
}

bool TCPProtocolClient::ConnectClose(TCPNetClient *con)
{
    if (con != BaseCon)
        return false;

    return OnConnectClose();
}

TCPNetClient *TCPProtocolClient::GetBaseCon()
{
    return BaseCon;
}
