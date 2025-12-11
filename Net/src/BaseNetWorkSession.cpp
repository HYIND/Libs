#include "Session/BaseNetWorkSession.h"

BaseNetWorkSession::BaseNetWorkSession()
    : isHandshakeComplete(false)
{
}

BaseNetWorkSession::~BaseNetWorkSession()
{
    isHandshakeComplete = false;
    _callbackRecvData = nullptr;
    _callbackSessionClose = nullptr;
}

bool BaseNetWorkSession::Connect(const std::string &IP, uint16_t Port)
{
    Release();

    bool result = BaseClient->Connect(IP, Port);
    if (!result)
        return false;

    BaseClient->BindMessageCallBack(std::bind(&BaseNetWorkSession::RecvData, this, std::placeholders::_1, std::placeholders::_2));
    // 尝试握手，超时时间10秒
    if (!TryHandshake(10 * 1000))
    {
        std::cout << "BaseNetWorkSession::Connect TryHandshake Connect Fail! CloseConnection\n";
        Release();
        return false;
    }

    BaseClient->BindCloseCallBack(std::bind(&BaseNetWorkSession::SessionClose, this, std::placeholders::_1));
    return true;
}

bool BaseNetWorkSession::Release()
{
    isHandshakeComplete = false;
    _callbackRecvData = nullptr;
    _callbackSessionClose = nullptr;
    return BaseClient->Release();
}

void BaseNetWorkSession::BindRecvDataCallBack(std::function<void(BaseNetWorkSession *, Buffer *recv, Buffer *response)> callback)
{
    _callbackRecvData = callback;
    OnBindRecvDataCallBack();
}
void BaseNetWorkSession::BindSessionCloseCallBack(std::function<void(BaseNetWorkSession *)> callback)
{
    _callbackSessionClose = callback;
    OnBindSessionCloseCallBack();
}

char *BaseNetWorkSession::GetIPAddr()
{
    return BaseClient->GetBaseCon()->GetIPAddr();
}

uint16_t BaseNetWorkSession::GetPort()
{
    return BaseClient->GetBaseCon()->GetPort();
}

void BaseNetWorkSession::RecvData(TCPEndPoint *client, Buffer *buffer)
{
    if (client != BaseClient)
        return;
    OnRecvData(buffer);
}

void BaseNetWorkSession::SessionClose(TCPEndPoint *client)
{
    if (client != BaseClient)
        return;

    OnSessionClose();
}

TCPEndPoint *BaseNetWorkSession::GetBaseClient()
{
    return BaseClient;
}
