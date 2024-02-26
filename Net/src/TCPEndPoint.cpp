#include "EndPoint/TCPEndPoint.h"
#include "EndPoint/PureTCPClient.h"
#include "EndPoint/WebSocketClient.h"

TcpProtocolListener::TcpProtocolListener(TCPNetProtocol proto) : _Protocol(proto)
{
    BaseListener.BindAcceptCallBack(std::bind(&TcpProtocolListener::RecvCon, this, std::placeholders::_1));
}
TcpProtocolListener::~TcpProtocolListener()
{
}

TCPNetProtocol TcpProtocolListener::Protocol()
{
    return _Protocol;
}

void TcpProtocolListener::SetProtocol(const TCPNetProtocol &proto)
{
    _Protocol = proto;
}

bool TcpProtocolListener::Listen(const std::string &IP, int Port)
{
    return BaseListener.Listen(IP, Port);
}

void TcpProtocolListener::BindEstablishConnectionCallBack(std::function<void(TCPEndPoint *)> callback)
{
    _callBackEstablish = callback;
}

void TcpProtocolListener::RecvCon(TCPTransportConnection *waitCon)
{
    switch (_Protocol)
    {
    case TCPNetProtocol::PureTCP:
    {

        TCPEndPoint *client = new PureTCPClient(waitCon);
        waitClients.emplace(client);
        waitCon->BindBufferCallBack(std::bind(&TcpProtocolListener::Handshake, this, std::placeholders::_1, std::placeholders::_2));
        break;
    }
    case TCPNetProtocol::WebSocket:
    {
        TCPEndPoint *client = new WebSocketClient(waitCon);
        waitClients.emplace(client);
        waitCon->BindBufferCallBack(std::bind(&TcpProtocolListener::Handshake, this, std::placeholders::_1, std::placeholders::_2));
        break;
    }
    default:
        std::cout << "TcpProtocolListener::RecvCon ,Protocol Error!\n";
        break;
    }
}

void TcpProtocolListener::Handshake(TCPTransportConnection *waitCon, Buffer *buf)
{
    waitClients.EnsureCall(
        [&](std::vector<TCPEndPoint *> &array) -> void
        {
            for (auto it = array.begin(); it != array.end(); it++)
            {
                TCPEndPoint *client = *it;
                if (client->GetBaseCon() != waitCon)
                    continue;

                {
                    CheckHandshakeStatus result = client->CheckHandshakeTryMsg(*buf); // 由Listener负责调用具体的Client握手方法，并监听请结果
                    if (result == CheckHandshakeStatus::Success)
                    {
                        if (_callBackEstablish)
                            _callBackEstablish(client);
                        array.erase(it);
                        waitCon->BindBufferCallBack(std::bind(&TCPEndPoint::RecvBuffer, client, std::placeholders::_1, std::placeholders::_2));
                        waitCon->BindRDHUPCallBack(std::bind(&TCPEndPoint::ConnectClose, client, std::placeholders::_1));
                        if (buf->Remaind() > 0)
                            client->RecvBuffer(waitCon, buf);
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

TCPEndPoint::TCPEndPoint()
{
}
TCPEndPoint::~TCPEndPoint()
{
    if (BaseCon)
        BaseCon->Release();
}

bool TCPEndPoint::Connect(const std::string &IP, uint16_t Port)
{
    if (!BaseCon->Connect(IP, Port))
        return false;

    BaseCon->BindBufferCallBack(std::bind(&TCPEndPoint::RecvBuffer, this, std::placeholders::_1, std::placeholders::_2));
    BaseCon->BindRDHUPCallBack(std::bind(&TCPEndPoint::ConnectClose, this, std::placeholders::_1));

    return true;
}

bool TCPEndPoint::Release()
{
    if (!BaseCon)
        return true;
    bool result = BaseCon->Release();
    isHandshakeComplete = false;
    _callbackMessage = nullptr;
    _callbackClose = nullptr;
    return result;
}
void TCPEndPoint::BindMessageCallBack(std::function<void(TCPEndPoint *, Buffer *)> callback)
{
    _callbackMessage = callback;
    OnBindMessageCallBack();
}
void TCPEndPoint::BindCloseCallBack(std::function<void(TCPEndPoint *)> callback)
{
    _callbackClose = callback;
    OnBindCloseCallBack();
}

bool TCPEndPoint::RecvBuffer(TCPTransportConnection *con, Buffer *buffer)
{
    if (con != BaseCon)
        return false;
    return OnRecvBuffer(buffer);
}

bool TCPEndPoint::ConnectClose(TCPTransportConnection *con)
{
    if (con != BaseCon)
        return false;

    return OnConnectClose();
}

TCPTransportConnection *TCPEndPoint::GetBaseCon()
{
    return BaseCon;
}
