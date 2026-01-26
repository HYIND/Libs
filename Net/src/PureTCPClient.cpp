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
        BaseCon = std::shared_ptr<TCPTransportConnection>(con);
    else
        BaseCon = std::make_shared<TCPTransportConnection>();
}

PureTCPClient::PureTCPClient(std::shared_ptr<TCPTransportConnection> con)
{
    Protocol = TCPNetProtocol::PureTCP;
    BaseCon = con;
}

PureTCPClient::~PureTCPClient()
{
    Release();
}

bool PureTCPClient::Connect(const std::string &IP, uint16_t Port)
{
    return Base::Connect(IP, Port);
}

#ifdef _linux_
Task<bool> PureTCPClient::ConnectAsync(const std::string &IP, uint16_t Port)
{
    co_return co_await Base::ConnectAsync(IP, Port);
}
#endif

bool PureTCPClient::Release()
{
    std::lock_guard<SpinLock> lock(_ProcessLock);
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

    if (buffer->Remain() > 0)
        cacheBuffer.Append(*buffer);

    std::lock_guard<SpinLock> lock(_ProcessLock);
    ProcessCacheBuffer();

    return true;
}

void PureTCPClient::ProcessCacheBuffer()
{
    if (_callbackMessage)
    {
        _callbackMessage(this, &cacheBuffer);
        cacheBuffer.Release();
    }
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

#ifdef _linux_
Task<bool> PureTCPClient::TryHandshakeAsync(uint32_t timeOutMs)
{
    co_return true;
}
#endif

CheckHandshakeStatus PureTCPClient::CheckHandshakeTryMsg(Buffer &buffer)
{
    isHandshakeComplete = true;
    return CheckHandshakeStatus::Success;
}

CheckHandshakeStatus PureTCPClient::CheckHandshakeConfirmMsg(Buffer &buffer)
{
    isHandshakeComplete = true;
    return CheckHandshakeStatus::Success;
}

void PureTCPClient::OnBindMessageCallBack()
{
    if (_ProcessLock.trylock())
    {
        try
        {
            ProcessCacheBuffer();
        }
        catch (const std::exception &e)
        {
            _ProcessLock.unlock();
            std::cerr << e.what() << '\n';
        }
        _ProcessLock.unlock();
    }
}

void PureTCPClient::OnBindCloseCallBack()
{
}
