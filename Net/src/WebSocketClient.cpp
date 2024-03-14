#include "EndPoint/WebSocketClient.h"
#include "ProtocolHelper/WebSocketHeleper.h"

const uint32_t maskNum = 20806633;

using Base = TCPEndPoint;

WebSocketClient::WebSocketClient(TCPTransportConnection *con)
{
    Protocol = TCPNetProtocol::WebSocket;
    cachePak = new WebSocketPackage();
    if (con)
        BaseCon = con;
    else
        BaseCon = new TCPTransportConnection();
}

WebSocketClient::~WebSocketClient()
{
    Release();
    SAFE_DELETE(BaseCon);
    SAFE_DELETE(cachePak);
}

bool WebSocketClient::Connect(const std::string &IP, uint16_t Port)
{
    if (!Base::Connect(IP, Port))
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

    isHandshakeComplete = false;

    cacheBuffer.Release();
    cachePak->buffer.Release();
    _SecWsKey.clear();

    WebSocketPackage *pak = nullptr;
    while (_RecvPaks.dequeue(pak))
    {
        SAFE_DELETE(pak);
    }
    pak = nullptr;
    while (_SendPaks.dequeue(pak))
    {
        SAFE_DELETE(pak);
    }

    return result;
}

bool WebSocketClient::OnRecvBuffer(Buffer *buffer)
{
    if (!isHandshakeComplete)
    {
        if (CheckHandshakeConfirmMsg(*buffer) != CheckHandshakeStatus::Success)
            return false;
    }

    if (buffer->Remaind() > 0)
        cacheBuffer.Append(*buffer);

    while (cacheBuffer.Remaind() > 0)
    {
        // 解析数据帧格式
        WebSocketDataframe dataFrame;
        int dataFameResult = WebSocketAnalysisHelp::AnalysisDataframe(cacheBuffer, dataFrame);
        if (dataFameResult == 1)
        {
            cacheBuffer.Shift(cacheBuffer.Postion());

            // 数据帧解析成功，将payload追加到当前的cachePak的末尾
            // 取出payload
            Buffer &payload = dataFrame.payload;
            cachePak->buffer.Append(payload);

            // fin=1,则获取到了完整的包,取出cachePak
            if (dataFrame.fin == 1 && dataFrame.opcode != (uint8_t)WSOpcodeType::WSOpcode_Continue)
            {
                cachePak->opcode = (WSOpcodeType)dataFrame.opcode;
                cachePak->buffer.Seek(0);
                WebSocketPackage *newPak = cachePak;
                cachePak = new WebSocketPackage();

                if (_ProcessLock.trylock())
                {
                    try
                    {
                        ProcessPakage(newPak);
                    }
                    catch (const std::exception &e)
                    {
                        std::cerr << e.what() << '\n';
                    }
                    _ProcessLock.unlock();
                }
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

void WebSocketClient::OnBindMessageCallBack()
{
    if (_ProcessLock.trylock())
    {
        try
        {
            ProcessPakage();
        }
        catch (const std::exception &e)
        {
            std::cerr << e.what() << '\n';
        }
        _ProcessLock.unlock();
    }
}

bool WebSocketClient::Send(const Buffer &buffer)
{
    try
    {
        if (!buffer.Data() || buffer.Length() < 0)
            return true;

        Buffer buf, WSFrameBuf;
        buf.CopyFromBuf(buffer);
        WebSocketAnalysisHelp::GenerateDataFrameBuffer(buf, (uint8_t)WSOpcodeType::WSOpcode_Text, 0, WSFrameBuf, maskNum);

        bool result = BaseCon->Send(WSFrameBuf);
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

    cacheBuffer.WriteFromOtherBufferPos(buffer);

    std::cout << "Recv HandShakeRequest:" << (char *)buffer.Data() << std::endl;
    std::map<std::string, std::string> params;
    int HsBufferLength = 0;
    int result = WebSocketAnalysisHelp::AnalysisHandshakeRequest(cacheBuffer, _SecWsKey, params, HsBufferLength);

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

        cacheBuffer.Shift(HsBufferLength);
        cacheBuffer.Seek(0);

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

    cacheBuffer.WriteFromOtherBufferPos(buffer);

    std::cout << "Recv HandShakeResponse:" << (char *)buffer.Data() << std::endl;
    std::map<std::string, std::string> params;
    int HsBufferLength = 0;
    int result = WebSocketAnalysisHelp::AnalysisHandshakeResponse(cacheBuffer, _SecWsKey, params, HsBufferLength);

    if (result == 1 && HsBufferLength > 0)
    {
        std::cout << "WebSocketClient - handshake successful!" << std::endl
                  << std::endl;

        cacheBuffer.Shift(HsBufferLength);
        cacheBuffer.Seek(0);

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

void WebSocketClient::ProcessPakage(WebSocketPackage *newpak)
{

    if (newpak)
    {
        if (newpak->opcode == WSOpcodeType::WSOpcode_Ping)
        {
            Buffer PongBuf;
            WebSocketAnalysisHelp::GenerateSpecialDataFrameBuffer((uint8_t)WSOpcodeType::WSOpcode_Pong, PongBuf, &(newpak->buffer));
            BaseCon->Send(PongBuf);
            SAFE_DELETE(newpak);
        }

        if (_RecvPaks.size() > 300)
        {
            WebSocketPackage *pak = nullptr;
            if (_RecvPaks.dequeue(pak))
                SAFE_DELETE(pak);
        }
        _RecvPaks.enqueue(newpak);
    }

    if (!_callbackMessage)
        return;

    int count = 10;
    WebSocketPackage *pak = nullptr;
    while (_RecvPaks.front(pak) && count > 0)
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
            std::cout << "error package opcode :WSOpcode_Continue\n";
        }
        break;
        case WSOpcodeType::WSOpcode_Text:
        {
            if (_callbackMessage)
                _callbackMessage(this, &pak->buffer);
        }
        break;
        case WSOpcodeType::WSOpcode_Binary:
        {
            if (_callbackMessage)
                _callbackMessage(this, &pak->buffer);
        }
        break;
        default:
            std::cout << "error package opcode :" << (int)pak->opcode << "\n";
            break;
        }
        if (!isHandshakeComplete)
            return;
        _RecvPaks.dequeue(pak);
        SAFE_DELETE(pak);
        count--;
    }
}