#include "EndPoint/PureTCPClient.h"

struct MsgHeader
{
    int seq = 0;
    int ack = -1;
    int length = 0;
};

using Base = TCPEndPoint;

PureTCPClient::PureTCPClient(TCPTransportConnection *con)
{
    Protocol = TCPNetProtocol::PureTCP;
    if (con)
        BaseCon = con;
    else
        BaseCon = new TCPTransportConnection();
}

PureTCPClient::~PureTCPClient()
{
    Release();
    SAFE_DELETE(BaseCon);
}

bool PureTCPClient::Connect(const std::string &IP, uint16_t Port)
{
    return Base::Connect(IP, Port);
}

bool PureTCPClient::Release()
{
    cacheBuffer.Release();
    return Base::Release();
}

bool PureTCPClient::OnRecvBuffer(Buffer *buffer)
{
    if (!isHandshakeComplete)
    {
        if (CheckHandshakeConfirmMsg(*buffer) != CheckHandshakeStatus::Success)
            return false;
    }

    if (buffer->Remaind() > 0)
        cacheBuffer.Append(*buffer);

    if (_callbackMessage)
    {
        _callbackMessage(this, &cacheBuffer);
        cacheBuffer.Release();
    }

    return true;
}

bool PureTCPClient::OnConnectClose()
{
    auto callback = _callbackClose;
    Release();
    if (callback)
        callback(this);
    return true;
}

bool PureTCPClient::Send(const Buffer &buffer)
{
    try
    {
        if (!buffer.Data() || buffer.Length() < 0)
            return true;
        return BaseCon->Send(buffer);
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return false;
    }
}

bool PureTCPClient::TryHandshake(uint32_t timeOutMs)
{
    return true;
}

CheckHandshakeStatus PureTCPClient::CheckHandshakeTryMsg(Buffer &buffer)
{
    if (isHandshakeComplete)
    {
        return CheckHandshakeStatus::None;
    }

    return CheckHandshakeStatus::Success;
}

CheckHandshakeStatus PureTCPClient::CheckHandshakeConfirmMsg(Buffer &buffer)
{
    if (isHandshakeComplete)
    {
        return CheckHandshakeStatus::None;
    }

    return CheckHandshakeStatus::Success;
}
