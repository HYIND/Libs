#include "ProtocolEndPoint/CustomTcpProtocolClient.h"

const char CustomProtocolTryToken[] = "alskdjfhg";      // 客户端发起连接发送的请求Token
const char CustomProtocolConfirmToken[] = "qpwoeiruty"; // 服务端接收到请求后返回的确认Token

const char HeartBuffer[] = "23388990";

struct MsgHeader
{
    int seq = 0;
    int ack = -1;
    int length = 0;
};

bool IsHeartBeat(const Buffer &Buffer)
{
    static int HeartSize = sizeof(HeartBuffer) - 1;
    return (Buffer.Length() == HeartSize && 0 == strncmp(HeartBuffer, Buffer.Byte(), HeartSize));
}

using Base = TCPProtocolClient;

CustomTCPProtocolClient::CustomTCPProtocolClient(TCPNetClient *con)
{
    Protocol = TCPNetProtocol::CustomProtocol;
    if (con)
        BaseCon = con;
    else
        BaseCon = new TCPNetClient();
}

CustomTCPProtocolClient::~CustomTCPProtocolClient()
{
    Release();
    SAFE_DELETE(BaseCon);
}

bool CustomTCPProtocolClient::Connet(const std::string &IP, uint16_t Port)
{
    if (!Base::Connet(IP, Port))
        return false;

    // 尝试握手，超时时间10秒
    if (!TryHandshake(10 * 1000))
    {
        std::cout << "CustomTCPProtocolClient::TryHandshake Connect Fail! CloseConnection\n";
        Release();
        return false;
    }

    return true;
}

bool CustomTCPProtocolClient::Release()
{
    bool result = Base::Release();

    CustomPackage *pak = nullptr;
    while (_RecvPaks.dequeue(pak))
        SAFE_DELETE(pak);
    while (_SendPaks.dequeue(pak))
        SAFE_DELETE(pak);

    _AwaitMap.EnsureCall(
        [&](std::map<int, AwaitTask *> &map) -> void
        {
            for (auto it = map.begin(); it != map.end(); it++)
            {
                SAFE_DELETE(it->second);
            }
            map.clear();
        });

    _tryHandshakeCV.notify_all();

    return result;
}

bool CustomTCPProtocolClient::OnRecvBuffer(Buffer *buffer)
{
    if (!isHandshakeComplete)
    {
        CheckHandshakeConfirmMsg(*buffer);
        return false;
    }

    CustomPackage *pak = nullptr;
    int left = buffer->Length() - buffer->Postion();
    int read = 0;
    while (left > 0)
    {
        if (!_RecvPaks.back(pak) || pak->enable)
        {
            if (_RecvPaks.size() > 300)
            {
                CustomPackage *pak = nullptr;
                if (_RecvPaks.dequeue(pak))
                    SAFE_DELETE(pak);
            }
            pak = new CustomPackage();
            pak->enable = false;
            _RecvPaks.enqueue(pak);
        }

        if (pak->read < sizeof(MsgHeader))
        {
            int min = std::min((int)(sizeof(MsgHeader) - pak->read), left);
            pak->buffer.WriteFromOtherBufferPos(*buffer, min);
            // pak->buffer.Write((char *)(buffer->Data()) + read, min);
            read += min;
            left -= min;
            pak->read += min;
            if (pak->read == sizeof(MsgHeader))
            {
                MsgHeader header;
                memcpy(&header, pak->buffer.Data(), sizeof(MsgHeader));
                pak->seq = header.seq;
                pak->ack = header.ack;
                pak->buffer.ReSize(header.length);
                pak->buffer.Seek(0);
            }
            else
            {
                // int i = 1;
                // i++;
            }
        }
        else
        {
            int pakBuffeHasRead = pak->read - sizeof(MsgHeader);
            int pakBufferNeedRead = pak->buffer.Length() - pakBuffeHasRead;
            int min = std::min(pakBufferNeedRead, left);
            pak->buffer.WriteFromOtherBufferPos(*buffer, min);
            read += min;
            left -= min;
            pak->read += min;
            if (min == pakBufferNeedRead)
            {
                pak->enable = true;
                if (pak->ack != -1)
                {
                    AwaitTask *task = nullptr;
                    if (_AwaitMap.Find(pak->ack, task))
                    {
                        if (task->respsonse)
                            task->respsonse->CopyFromBuf(pak->buffer);

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
                else if (_callbackMessage)
                {
                    Buffer resposne;
                    _callbackMessage(this, &pak->buffer, &resposne);
                    if (resposne.Length() > 0)
                        this->AsyncSend(resposne, pak->seq);
                    resposne.Release();
                }
            }
            else
            {
                // int i = 1;
                // i++;
            }
        }
    }
    return true;
}

bool CustomTCPProtocolClient::OnConnectClose()
{
    auto callback = _callbackClose;
    Release();
    if (callback)
        callback(this);
    return true;
}

bool CustomTCPProtocolClient::AsyncSend(const Buffer &buffer, int ack)
{
    try
    {
        if (!buffer.Data() || buffer.Length() < 0)
            return true;
        MsgHeader header;
        int seq = this->seq++;
        header.seq = seq;
        header.ack = ack;
        header.length = buffer.Length();
        Buffer buf(sizeof(MsgHeader) + buffer.Length());
        buf.Write(&header, sizeof(MsgHeader));
        buf.Write(buffer);

        return BaseCon->Send(buf);
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return false;
    }
}

bool CustomTCPProtocolClient::AwaitSend(const Buffer &buffer, Buffer &response)
{
    try
    {
        if (!buffer.Data() || buffer.Length() < 0)
            return true;

        bool result = false;
        AwaitTask *task = new AwaitTask();
        task->respsonse = &response;

        MsgHeader header;
        int seq = this->seq++;
        header.seq = seq;
        header.ack = -1;
        header.length = buffer.Length();
        Buffer buf = Buffer(sizeof(MsgHeader) + buffer.Length());
        buf.Write(&header, sizeof(MsgHeader));
        buf.Write(buffer);

        task->seq = header.seq;
        if (!_AwaitMap.Insert(task->seq, task))
            return false;

        if (BaseCon->Send(buf)) // 发送
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

bool CustomTCPProtocolClient::TryHandshake(uint32_t timeOutMs)
{
    std::unique_lock<std::mutex> lock(_tryHandshakeMutex);

    Buffer token(CustomProtocolTryToken, sizeof(CustomProtocolTryToken) - 1);
    if (!BaseCon->Send(token))
        return false;

    // 等待握手结果，超时或者被唤醒时触发，返回isHandshakeComplete
    return _tryHandshakeCV.wait_for(lock,
                                    std::chrono::milliseconds(timeOutMs),
                                    [this]
                                    { return isHandshakeComplete; });
}

CheckHandshakeStatus CustomTCPProtocolClient::CheckHandshakeTryMsg(Buffer &buffer)
{
    if (isHandshakeComplete)
    {
        return CheckHandshakeStatus::None;
    }

    bool inQueue = false;
    CustomPackage *pak = nullptr;
    if (!_RecvPaks.empty())
    {
        inQueue = true;
        _RecvPaks.front(pak);
    }
    else
    {
        inQueue = false;
        pak = new CustomPackage();
    }

    pak->buffer.WriteFromOtherBufferPos(buffer);

    int tokenLength = sizeof(CustomProtocolTryToken) - 1;
    if (pak->buffer.Length() < tokenLength)
        return CheckHandshakeStatus::BufferAgain;

    if (strncmp((char *)pak->buffer.Data(), CustomProtocolTryToken, tokenLength) != 0)
        return CheckHandshakeStatus::Fail;

    Buffer rsp(CustomProtocolConfirmToken, sizeof(CustomProtocolConfirmToken) - 1);
    if (!BaseCon->Send(rsp))
        return CheckHandshakeStatus::Fail;

    if (pak->buffer.Length() > tokenLength)
    {
        int remaindLength = pak->buffer.Length() - tokenLength;
        pak->buffer.Seek(0);
        pak->buffer.Write((char *)pak->buffer.Data() + tokenLength, remaindLength);
        pak->buffer.ReSize(remaindLength);
        if (!inQueue)
            _RecvPaks.enqueue(pak);
    }
    else if (inQueue)
    {
        _RecvPaks.dequeue(pak);
    }

    isHandshakeComplete = true;
    return CheckHandshakeStatus::Success;
}

CheckHandshakeStatus CustomTCPProtocolClient::CheckHandshakeConfirmMsg(Buffer &buffer)
{
    if (isHandshakeComplete)
    {
        return CheckHandshakeStatus::None;
    }

    bool inQueue = false;
    CustomPackage *pak = nullptr;
    if (!_RecvPaks.empty())
    {
        inQueue = true;
        _RecvPaks.front(pak);
    }
    else
    {
        inQueue = false;
        pak = new CustomPackage();
    }

    pak->buffer.WriteFromOtherBufferPos(buffer);

    int tokenLength = sizeof(CustomProtocolConfirmToken) - 1;
    if (pak->buffer.Length() < tokenLength)
        return CheckHandshakeStatus::BufferAgain;

    if (strncmp((char *)pak->buffer.Data(), CustomProtocolConfirmToken, tokenLength) != 0)
        return CheckHandshakeStatus::Fail;

    if (pak->buffer.Length() > tokenLength)
    {
        int remaindLength = pak->buffer.Length() - tokenLength;
        pak->buffer.Seek(0);
        pak->buffer.Write((char *)pak->buffer.Data() + tokenLength, remaindLength);
        pak->buffer.ReSize(remaindLength);
        if (!inQueue)
            _RecvPaks.enqueue(pak);
    }
    else if (inQueue)
    {
        _RecvPaks.dequeue(pak);
    }

    isHandshakeComplete = true;
    _tryHandshakeCV.notify_all();
    return CheckHandshakeStatus::Success;
}
