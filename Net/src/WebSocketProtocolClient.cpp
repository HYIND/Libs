#include "ProtocolEndPoint/WebSocketProtocolClient.h"
#include "ProtocolHelper/WebSocketProtocolHeleper.h"

const uint32_t maskNum = 20806633;

struct MsgHeader
{
    int seq = 0;
    int ack = -1;
};

using Base = TCPProtocolClient;

// 处理包内容，从包的Buffer中取出seq、ack，使用者获取的Buffer中不应包含相应字段
void WebSocketShiftPakHeader(WebSocketPakage *pak)
{
    if (!pak || pak->buffer.Length() < 8)
        return;
    pak->buffer.Read(&(pak->seq), 4);
    pak->buffer.Read(&(pak->ack), 4);
    pak->buffer.Shift(8);
}
// 处理流内容，在流的头部添加seq和ack字段
void WebSocketAddPakHeader(Buffer *buf, int seq, int ack)
{
    if (!buf)
        return;
    int header[2] = {seq, ack};
    buf->Unshift(header, 8);
    buf->Seek(buf->Postion() + 8);
}

WebSocketClient::WebSocketClient(TCPNetClient *con)
{
    Protocol = TCPNetProtocol::WebSocket;
    if (con)
        BaseCon = con;
    else
        BaseCon = new TCPNetClient();
}

WebSocketClient::~WebSocketClient()
{
    Release();
    SAFE_DELETE(BaseCon);
}

bool WebSocketClient::Connet(const std::string &IP, uint16_t Port)
{
    if (!Base::Connet(IP, Port))
        return false;

    // 尝试握手，超时时间10秒
    if (!TryHandshake(10 * 1000))
    {
        std::cout << "WebSocketClient::TryHandshake Connect Fail! CloseConnection\n";
        Release();
        return false;
    }

    return true;
}

bool WebSocketClient::Release()
{
    bool result = Base::Release();

    WebSocketPakage *pak = nullptr;
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

    cacheBuffer.Release();
    _SecWsKey.clear();
    _tryHandshakeCV.notify_all();

    return result;
}
bool WebSocketClient::OnRecvBuffer(Buffer *buffer)
{
    if (!isHandshakeComplete)
    {
        CheckHandshakeConfirmMsg(*buffer);
        return false;
    }

    cacheBuffer.Append(*buffer);

    while (cacheBuffer.Remaind() > 0)
    {
        // 解析数据帧格式
        WebSocketDataframe dataFrame;
        int dataFameResult = WebSocketAnalysisHelp::AnalysisDataframe(cacheBuffer, dataFrame);
        if (dataFameResult == 1)
        {
            cacheBuffer.Shift(cacheBuffer.Postion());

            // 数据帧解析成功，将payload追加到当前不完整的pak的末尾
            WebSocketPakage *pak = nullptr;
            if (!_RecvPaks.back(pak) || pak->enable)
            {
                if (_RecvPaks.size() > 300)
                {
                    WebSocketPakage *pak = nullptr;
                    if (_RecvPaks.dequeue(pak))
                        SAFE_DELETE(pak);
                }
                pak = new WebSocketPakage();
                pak->enable = false;
                _RecvPaks.enqueue(pak);
            }

            // 取出payload
            Buffer &payload = dataFrame.payload;
            pak->buffer.Append(payload);

            // fin=1,则获取到了完整的包
            if (dataFrame.fin == 1 && dataFrame.opcode != (uint8_t)WSOpcodeType::WSOpcode_Continue)
            {
                pak->opcode = (WSOpcodeType)dataFrame.opcode;
                pak->enable = true;
                ProcessPakage();
            }
        }
        else if (dataFameResult == -1)
        {
            break;
        }
        else
        {
            std::cout << "dataFameFormat Error!\n";
            Release();
        }
    }
    return true;
}

bool WebSocketClient::OnConnectClose()
{
    auto callback = _callbackClose;
    Release();
    if (callback)
        callback(this);
    return true;
}

bool WebSocketClient::AsyncSend(const Buffer &buffer, int ack)
{
    try
    {
        if (!buffer.Data() || buffer.Length() < 0)
            return true;
        MsgHeader header;
        int seq = this->seq++;
        header.seq = seq;
        header.ack = ack;

        Buffer buf;
        WebSocketAddPakHeader(&buf, header.seq, header.ack);

        buf.Write(buffer);

        Buffer WSFrameBuf;
        WebSocketAnalysisHelp::GenerateDataFrameBuffer(buf, (uint8_t)WSOpcodeType::WSOpcode_Text, 0, WSFrameBuf, maskNum);

        // WebSocketDataframe info;
        // WebSocketAnalysisHelp::AnalysisDataframe(WSFrameBuf, info);

        bool result = BaseCon->Send(WSFrameBuf);
        return result;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return false;
    }
}

bool WebSocketClient::AwaitSend(const Buffer &buffer, Buffer &response)
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

        Buffer buf;
        WebSocketAddPakHeader(&buf, header.seq, header.ack);

        buf.Write(buffer);

        Buffer WSFrameBuf;
        WebSocketAnalysisHelp::GenerateDataFrameBuffer(buf, (uint8_t)WSOpcodeType::WSOpcode_Text, 0, WSFrameBuf, maskNum);

        task->seq = header.seq;
        if (!_AwaitMap.Insert(task->seq, task))
            return false;

        if (BaseCon->Send(WSFrameBuf)) // 发送
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

bool WebSocketClient::TryHandshake(uint32_t timeOutMs)
{
    std::unique_lock<std::mutex> lock(_tryHandshakeMutex);

    std::string request;
    WebSocketAnalysisHelp::GenerateHandshakeRequest(request, _SecWsKey);
    Buffer req(request);
    if (!BaseCon->Send(req))
        return false;

    // std::string wskey;
    // std::map<std::string, std::string> params;
    // int length;
    // WebSocketAnalysisHelp::AnalysisHandshakeRequest(req, wskey, params, length);

    // std::string resp;
    // WebSocketAnalysisHelp::GenerateHandshakeResponse(params, _SecWsKey, resp);

    // 等待握手结果，超时或者被唤醒时触发，返回isHandshakeComplete
    return _tryHandshakeCV.wait_for(lock,
                                    std::chrono::milliseconds(timeOutMs),
                                    [this]
                                    { return isHandshakeComplete; });
}

CheckHandshakeStatus WebSocketClient::CheckHandshakeTryMsg(Buffer &buffer)
{
    if (isHandshakeComplete)
    {
        return CheckHandshakeStatus::None;
    }

    bool inQueue = false;
    WebSocketPakage *pak = nullptr;
    if (!_RecvPaks.empty())
    {
        inQueue = true;
        _RecvPaks.front(pak);
    }
    else
    {
        inQueue = false;
        pak = new WebSocketPakage();
    }

    pak->buffer.WriteFromOtherBufferPos(buffer);

    std::cout << "Recv HandShakeRequest:" << (char *)buffer.Data() << std::endl;
    std::map<std::string, std::string> params;
    int HsBufferLength = 0;
    int result = WebSocketAnalysisHelp::AnalysisHandshakeRequest(pak->buffer, _SecWsKey, params, HsBufferLength);

    if (result == 1 && HsBufferLength > 0)
    {
        std::string handshakeRsponse;
        if (!WebSocketAnalysisHelp::GenerateHandshakeResponse(params, _SecWsKey, handshakeRsponse))
            return CheckHandshakeStatus::Fail;

        Buffer rsp(handshakeRsponse);
        if (!BaseCon->Send(rsp))
            return CheckHandshakeStatus::Fail;

        std::cout << "WebSocketClient - handshake successful!" << std::endl
                  << std::endl;

        if (pak->buffer.Length() > HsBufferLength)
        {
            int remaindLength = pak->buffer.Length() - HsBufferLength;
            pak->buffer.Seek(0);
            pak->buffer.Write((char *)pak->buffer.Data() + HsBufferLength, remaindLength);
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

    if (result == -1)
    {
        return CheckHandshakeStatus::BufferAgain;
    }

    return CheckHandshakeStatus::Fail;
}

CheckHandshakeStatus WebSocketClient::CheckHandshakeConfirmMsg(Buffer &buffer)
{
    if (isHandshakeComplete)
    {
        return CheckHandshakeStatus::None;
    }

    bool inQueue = false;
    WebSocketPakage *pak = nullptr;
    if (!_RecvPaks.empty())
    {
        inQueue = true;
        _RecvPaks.front(pak);
    }
    else
    {
        inQueue = false;
        pak = new WebSocketPakage();
    }

    pak->buffer.WriteFromOtherBufferPos(buffer);

    std::cout << "Recv HandShakeResponse:" << (char *)buffer.Data() << std::endl;
    std::map<std::string, std::string> params;
    int HsBufferLength = 0;
    int result = WebSocketAnalysisHelp::AnalysisHandshakeResponse(pak->buffer, _SecWsKey, params, HsBufferLength);

    if (result == 1 && HsBufferLength > 0)
    {
        std::cout << "WebSocketClient - handshake successful!" << std::endl
                  << std::endl;

        if (pak->buffer.Length() > HsBufferLength)
        {
            int remaindLength = pak->buffer.Length() - HsBufferLength;
            pak->buffer.Seek(0);
            pak->buffer.Write((char *)pak->buffer.Data() + HsBufferLength, remaindLength);
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

    if (result == -1)
    {
        return CheckHandshakeStatus::BufferAgain;
    }

    return CheckHandshakeStatus::Fail;
}

void WebSocketClient::ProcessPakage()
{
    int count = 10;
    WebSocketPakage *pak = nullptr;
    while (_RecvPaks.dequeue(pak) && count > 0)
    {
        switch (pak->opcode)
        {
        case WSOpcodeType::WSOpcode_Ping:
        {
            Buffer PongBuf;
            WebSocketAnalysisHelp::GenerateSpecialDataFrameBuffer((uint8_t)WSOpcodeType::WSOpcode_Pong, PongBuf, &(pak->buffer));
            BaseCon->Send(PongBuf);
        }
        break;
        case WSOpcodeType::WSOpcode_Pong:
        {
        }
        break;
        case WSOpcodeType::WSOpcode_Close:
        {
            Buffer CloseBuf;
            WebSocketAnalysisHelp::GenerateSpecialDataFrameBuffer((uint8_t)WSOpcodeType::WSOpcode_Close, CloseBuf, &(pak->buffer));
            BaseCon->Send(CloseBuf);
        }
        break;
        case WSOpcodeType::WSOpcode_Continue:
        {
        }
        break;
        case WSOpcodeType::WSOpcode_Text:
        {
            WebSocketShiftPakHeader(pak);
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
                    AsyncSend(resposne, pak->seq);
                resposne.Release();
            }
        }
        break;
        case WSOpcodeType::WSOpcode_Binary:
        {
            WebSocketShiftPakHeader(pak);
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
        break;
        default:
            break;
        }
        count--;
    }
}