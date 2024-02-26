#include "Session/CustomWebSocketSession.h"

struct CustomWSMsgHeader
{
    int seq = 0;
    int ack = -1;
};

using Base = BaseNetWorkSession;

// 处理包内容，从包的Buffer中取出seq、ack，使用者获取的Buffer中不应包含相应字段
void ShiftPakHeader(CustomWebSocketSessionPakage *pak)
{
    if (!pak || pak->buffer.Length() < 8)
        return;
    pak->buffer.Read(&(pak->seq), 4);
    pak->buffer.Read(&(pak->ack), 4);
    pak->buffer.Shift(8);
}
// 处理流内容，在流的头部添加seq和ack字段
void AddPakHeader(Buffer *buf, CustomWSMsgHeader header)
{
    if (!buf)
        return;
    buf->Unshift(&header, 8);
    buf->Seek(buf->Postion() + 8);
}

CustomWebSocketSession::CustomWebSocketSession(WebSocketClient *client)
{
    if (client)
        BaseClient = client;
    else
        BaseClient = new WebSocketClient();
}

CustomWebSocketSession::~CustomWebSocketSession()
{
    Release();
    SAFE_DELETE(BaseClient);
}
bool CustomWebSocketSession::Connect(const std::string &IP, uint16_t Port)
{
    Release();

    bool result = BaseClient->Connect(IP, Port);
    if (!result)
        return false;

    BaseClient->BindMessageCallBack(std::bind(&CustomWebSocketSession::RecvData, this, std::placeholders::_1, std::placeholders::_2));
    // 尝试握手，超时时间10秒
    if (!TryHandshake(10 * 1000))
    {
        std::cout << "CustomTcpSession::TryHandshake Connect Fail! CloseConnection\n";
        Release();
        return false;
    }

    BaseClient->BindCloseCallBack(std::bind(&CustomWebSocketSession::SessionClose, this, std::placeholders::_1));

    return true;
}

bool CustomWebSocketSession::Release()
{
    bool result = Base::Release();

    _AwaitMap.EnsureCall(
        [&](std::map<int, AwaitTask *> &map) -> void
        {
            for (auto it = map.begin(); it != map.end(); it++)
            {
                SAFE_DELETE(it->second);
            }
            map.clear();
        });

    return result;
}

bool CustomWebSocketSession::AsyncSend(const Buffer &buffer)
{
    return Send(buffer, -1);
}

bool CustomWebSocketSession::AwaitSend(const Buffer &buffer, Buffer &response)
{
    try
    {
        if (!buffer.Data() || buffer.Length() < 0)
            return true;

        bool result = false;

        int seq = this->seq++;

        AwaitTask *task = new AwaitTask();
        task->respsonse = &response;
        task->seq = seq;
        if (!_AwaitMap.Insert(task->seq, task))
            return false;

        Buffer buf(buffer);
        AddPakHeader(&buf, {seq, -1});
        if (BaseClient->Send(buf)) // 发送
        {
            std::unique_lock<std::mutex> awaitlck(task->_mtx);
            task->status = 1;
            // printf("wait_for , task->seq:%d\n", task->seq);
            result = task->_cv.wait_for(awaitlck, std::chrono::seconds(8)) != std::cv_status::timeout; // 等待返回并超时检查
        }

        task->status = 0;
        _AwaitMap.EnsureCall(
            [&](std::map<int, AwaitTask *> &map) -> void
            {
                auto it = map.find(task->seq);
                if (it != map.end())
                    map.erase(it);
                delete task;
            });
        return result;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return false;
    }
}

bool CustomWebSocketSession::OnSessionClose()
{
    auto callback = _callbackSessionClose;
    Release();
    if (callback)
        callback(this);
    return true;
}

bool CustomWebSocketSession::OnRecvData(Buffer *buffer)
{
    CustomWebSocketSessionPakage *pak = new CustomWebSocketSessionPakage();
    pak->buffer.CopyFromBuf(*buffer);
    ShiftPakHeader(pak);
    ProcessPakage(pak);

    return true;
}

void CustomWebSocketSession::OnBindRecvDataCallBack()
{
    return;
}

void CustomWebSocketSession::OnBindSessionCloseCallBack()
{
    return;
}

bool CustomWebSocketSession::TryHandshake(uint32_t timeOutMs)
{
    return true;
}

CheckHandshakeStatus CustomWebSocketSession::CheckHandshakeTryMsg(Buffer &buffer)
{
    return CheckHandshakeStatus::Success;
}

CheckHandshakeStatus CustomWebSocketSession::CheckHandshakeConfirmMsg(Buffer &buffer)
{
    return CheckHandshakeStatus::Success;
}

WebSocketClient *CustomWebSocketSession::GetBaseClient()
{
    return (WebSocketClient *)BaseClient;
}

bool CustomWebSocketSession::Send(const Buffer &buffer, int ack)
{
    try
    {
        if (!buffer.Data() || buffer.Length() < 0)
            return true;

        int seq = this->seq++;
        Buffer buf(buffer);
        AddPakHeader(&buf, {seq, ack});
        return BaseClient->Send(buf);
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return false;
    }
}

void CustomWebSocketSession::ProcessPakage(CustomWebSocketSessionPakage *newPak)
{
    if (!newPak)
        return;

    if (newPak->ack != -1)
    {
        AwaitTask *task = nullptr;
        if (_AwaitMap.Find(newPak->ack, task))
        {
            if (task->respsonse)
                task->respsonse->CopyFromBuf(newPak->buffer);

            int count = 0;
            while (!task->status == -1 && count < 5)
            {
                count++;
                usleep(10 * 1000);
            }
            usleep(5 * 1000);
            // printf("notify_all , pak->ack:%d\n", pak->ack);
            task->_cv.notify_all();
        }
    }
    else if (_callbackRecvData)
    {
        Buffer resposne;
        _callbackRecvData(this, &newPak->buffer, &resposne);
        if (resposne.Length() > 0)
            Send(resposne, newPak->seq);
        resposne.Release();
    }
}
