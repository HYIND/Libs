#include "EndPoint/TCPEndPoint.h"

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

#ifdef __linux__
Task<bool> TCPEndPoint::ConnectAsync(const std::string &IP, uint16_t Port)
{
    if (!co_await BaseCon->ConnectAsync(IP, Port))
        co_return false;

    BaseCon->BindBufferCallBack(std::bind(&TCPEndPoint::RecvBuffer, this, std::placeholders::_1, std::placeholders::_2));
    BaseCon->BindRDHUPCallBack(std::bind(&TCPEndPoint::ConnectClose, this, std::placeholders::_1));

    co_return true;
}
#endif

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
    if (con != BaseCon.get())
        return false;
    return OnRecvBuffer(buffer);
}

bool TCPEndPoint::ConnectClose(TCPTransportConnection *con)
{
    if (con != BaseCon.get())
        return false;

    return OnConnectClose();
}

std::shared_ptr<TCPTransportConnection> TCPEndPoint::GetBaseCon()
{
    return BaseCon;
}
