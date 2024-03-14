#include "Session/PureWebSocketSession.h"

using Base = BaseNetWorkSession;

PureWebSocketSession::PureWebSocketSession(WebSocketClient *client)
{
    if (client)
        BaseClient = client;
    else
        BaseClient = new WebSocketClient();
}

PureWebSocketSession::~PureWebSocketSession()
{
    Release();
    SAFE_DELETE(BaseClient);
}
bool PureWebSocketSession::Connect(const std::string &IP, uint16_t Port)
{
    Release();

    bool result = BaseClient->Connect(IP, Port);
    if (!result)
        return false;

    isHandshakeComplete = true;
    BaseClient->BindMessageCallBack(std::bind(&PureWebSocketSession::RecvData, this, std::placeholders::_1, std::placeholders::_2));
    /*     // // 尝试握手，超时时间10秒
        if (!TryHandshake(10 * 1000))
        {
            std::cout << "CustomTcpSession::TryHandshake Connect Fail! CloseConnection\n";
            Release();
            return false;
        } */

    BaseClient->BindCloseCallBack(std::bind(&PureWebSocketSession::SessionClose, this, std::placeholders::_1));

    return true;
}

bool PureWebSocketSession::Release()
{
    bool result = Base::Release();

    PureWebSocketSessionPakage *pak = nullptr;
    while (_RecvPaks.dequeue(pak))
    {
        _RecvPaks.dequeue(pak);
        SAFE_DELETE(pak);
    }

    return result;
}

bool PureWebSocketSession::AsyncSend(const Buffer &buffer)
{
    return Send(buffer);
}

bool PureWebSocketSession::OnSessionClose()
{
    auto callback = _callbackSessionClose;
    Release();
    if (callback)
        callback(this);
    return true;
}

bool PureWebSocketSession::OnRecvData(Buffer *buffer)
{
    PureWebSocketSessionPakage *pak = new PureWebSocketSessionPakage();
    pak->buffer.CopyFromBuf(*buffer);
    _ProcessLock.lock();
    ProcessPakage(pak);
    _ProcessLock.unlock();

    return true;
}

void PureWebSocketSession::OnBindRecvDataCallBack()
{
    if (_ProcessLock.trylock())
    {
        try
        {
            ProcessPakage();
        }
        catch (const std::exception &e)
        {
            _ProcessLock.unlock();
            std::cerr << e.what() << '\n';
        }
        _ProcessLock.unlock();
    }
}

void PureWebSocketSession::OnBindSessionCloseCallBack()
{
    return;
}

bool PureWebSocketSession::TryHandshake(uint32_t timeOutMs)
{
    return true;
}

CheckHandshakeStatus PureWebSocketSession::CheckHandshakeTryMsg(Buffer &buffer)
{
    isHandshakeComplete = true;
    return CheckHandshakeStatus::Success;
}

CheckHandshakeStatus PureWebSocketSession::CheckHandshakeConfirmMsg(Buffer &buffer)
{
    isHandshakeComplete = true;
    return CheckHandshakeStatus::Success;
}

WebSocketClient *PureWebSocketSession::GetBaseClient()
{
    return (WebSocketClient *)BaseClient;
}

bool PureWebSocketSession::Send(const Buffer &buffer)
{
    try
    {
        if (!buffer.Data() || buffer.Length() < 0)
            return true;

        Buffer buf(buffer);
        return BaseClient->Send(buf);
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return false;
    }
}

void PureWebSocketSession::ProcessPakage(PureWebSocketSessionPakage *newPak)
{
    if (newPak)
    {
        if (_RecvPaks.size() > 300)
        {
            PureWebSocketSessionPakage *pak = nullptr;
            if (_RecvPaks.dequeue(pak))
                SAFE_DELETE(pak);
        }
        _RecvPaks.enqueue(newPak);
    }

    int count = 10;
    PureWebSocketSessionPakage *pak = nullptr;
    while (_RecvPaks.front(pak) && count > 0)
    {
        if (_callbackRecvData)
        {
            Buffer resposne;
            _callbackRecvData(this, &pak->buffer, &resposne);
            if (!isHandshakeComplete)
                return;
            if (resposne.Length() > 0)
                Send(resposne);
            resposne.Release();
            _RecvPaks.dequeue(pak);
            SAFE_DELETE(pak);
        }
        count--;
    }
}
