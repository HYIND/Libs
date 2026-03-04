#include "EndPoint/TCPEndPoint.h"

TCPEndPoint::TCPEndPoint()
{
}
TCPEndPoint::~TCPEndPoint()
{
    if (BaseCon)
        BaseCon->Release();
}

Task<bool> TCPEndPoint::Connect(std::string IP, uint16_t Port)
{
    if (!co_await BaseCon->Connect(IP, Port))
        co_return false;

    BaseCon->BindBufferCallBack(std::bind(&TCPEndPoint::RecvBuffer, this, std::placeholders::_1, std::placeholders::_2));
    BaseCon->BindRDHUPCallBack(std::bind(&TCPEndPoint::ConnectClose, this, std::placeholders::_1));

    co_return true;
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
void TCPEndPoint::BindMessageCallBack(std::function<Task<void>(TCPEndPoint *, Buffer *)> callback)
{
    _callbackMessage = callback;
    OnBindMessageCallBack();
}
void TCPEndPoint::BindCloseCallBack(std::function<Task<void>(TCPEndPoint *)> callback)
{
    _callbackClose = callback;
    OnBindCloseCallBack();
}

Task<void> TCPEndPoint::RecvBuffer(TCPTransportConnection *con, Buffer *buffer)
{
    if (con != BaseCon.get())
        co_return;
    co_await OnRecvBuffer(buffer);
    co_return;
}

Task<void> TCPEndPoint::ConnectClose(TCPTransportConnection *con)
{
    if (con != BaseCon.get())
        co_return;
    co_await OnConnectClose();
    co_return;
}

std::shared_ptr<TCPTransportConnection> TCPEndPoint::GetBaseCon()
{
    return BaseCon;
}

void TCPEndPoint::SetHandShakeTimeOut(uint32_t ms)
{
    _handshaketimeOutMs = ms;
}
uint32_t TCPEndPoint::GetHandShakeTimeOut()
{
    return _handshaketimeOutMs;
}
