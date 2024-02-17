#pragma once

#include "Core/TcpTransportWarpper.h"

enum TCPNetProtocol
{
    CustomProtocol = 10,
    WebSocket = 20
};

// 关注返回值的等待任务
struct AwaitTask
{
    int seq = 0;
    Buffer *respsonse;
    std::mutex _mtx;
    std::condition_variable _cv;
    int status = -1; //-1无效，0等待超时，1等待中
    bool time_out = false;
};

enum CheckHandshakeStatus
{
    None = -1,
    Fail = 0,       // 失败
    Success = 1,    // 成功
    BufferAgain = 2 // 等待数据
};

class TCPProtocolClient;

// 用于监听指定协议的TCP连接，用于校验连接协议
class TcpProtocolListener
{
public:
    EXPORT_FUNC TcpProtocolListener(TCPNetProtocol proto = CustomProtocol);
    EXPORT_FUNC ~TcpProtocolListener();

    EXPORT_FUNC bool Listen(const std::string &IP, int Port);
    EXPORT_FUNC void BindEstablishConnectionCallBack(std::function<void(TCPProtocolClient *)> callback);

private:
    EXPORT_FUNC void RecvCon(TCPNetClient *waitClient);
    EXPORT_FUNC void Handshake(TCPNetClient *waitCon, Buffer *buf);

private:
    TCPNetListener BaseListener;
    TCPNetProtocol Protocol;
    std::function<void(TCPProtocolClient *)> _callBackEstablish;
    SafeArray<TCPProtocolClient *> waitClients; // 等待校验协议的客户端
};

class TCPProtocolClient
{
public:
    EXPORT_FUNC TCPProtocolClient();
    EXPORT_FUNC virtual ~TCPProtocolClient();

    EXPORT_FUNC virtual bool Connet(const std::string &IP, uint16_t Port);
    EXPORT_FUNC virtual bool Release();

    EXPORT_FUNC virtual bool OnRecvBuffer(Buffer *buffer) = 0;
    EXPORT_FUNC virtual bool OnConnectClose() = 0;

    EXPORT_FUNC virtual bool AsyncSend(const Buffer &buffer, int ack = -1) = 0;     // 异步发送，不关心返回结果
    EXPORT_FUNC virtual bool AwaitSend(const Buffer &buffer, Buffer &response) = 0; // 等待返回结果的发送，关心返回的结果

    EXPORT_FUNC void BindMessageCallBack(std::function<void(TCPProtocolClient *, Buffer *, Buffer *AckResponse)> callback);
    EXPORT_FUNC void BindCloseCallBack(std::function<void(TCPProtocolClient *)> callback);

    EXPORT_FUNC TCPNetClient *GetBaseCon();

public:
    // 2表示协议握手所需的字节流长度不足，0表示握手失败，关闭连接，1表示握手成功，建立连接
    EXPORT_FUNC virtual bool TryHandshake(uint32_t timeOutMs) = 0;                         // 作为发起连接的一方，主动发送握手信息
    EXPORT_FUNC virtual CheckHandshakeStatus CheckHandshakeTryMsg(Buffer &buffer) = 0;     // 作为接受连接的一方，检查连接发起者的握手信息，并返回回复信息
    EXPORT_FUNC virtual CheckHandshakeStatus CheckHandshakeConfirmMsg(Buffer &buffer) = 0; // 作为发起连接的一方，检查连接接受者的返回的回复信息，若确认则连接建立

    EXPORT_FUNC bool RecvBuffer(TCPNetClient *con, Buffer *buffer); // 用于绑定网络层(TCP/UDP)触发的Buffer回调
    EXPORT_FUNC bool ConnectClose(TCPNetClient *con);               // 用于绑定网络层(TCP/UDP)触发的RDHUP回调

protected:
    TCPNetProtocol Protocol;
    TCPNetClient *BaseCon;
    bool isHandshakeComplete = false;

    std::function<void(TCPProtocolClient *, Buffer *, Buffer *response)> _callbackMessage;
    std::function<void(TCPProtocolClient *)> _callbackClose;
};
