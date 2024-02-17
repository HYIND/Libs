#pragma once

#include "TCPProtocolEndPoint.h"

enum class WSOpcodeType : uint8_t
{
    WSOpcode_Continue = 0x0,
    WSOpcode_Text = 0x1,
    WSOpcode_Binary = 0x2,
    WSOpcode_Close = 0x8,
    WSOpcode_Ping = 0x9,
    WSOpcode_Pong = 0xA,
};

struct WebSocketPakage
{
    bool enable = true;

    int seq = 0;
    int ack = -1;
    WSOpcodeType opcode = WSOpcodeType::WSOpcode_Binary;
    Buffer buffer;
};

// 基于WebSocket应用层协议的客户端
class WebSocketClient : public TCPProtocolClient
{
public:
    EXPORT_FUNC WebSocketClient(TCPNetClient *con = nullptr);
    EXPORT_FUNC ~WebSocketClient();

public:
    EXPORT_FUNC virtual bool Connet(const std::string &IP, uint16_t Port);
    EXPORT_FUNC virtual bool Release();

    EXPORT_FUNC virtual bool OnRecvBuffer(Buffer *buffer); // 用于绑定网络层(TCP/UDP)触发的Buffer回调
    EXPORT_FUNC virtual bool OnConnectClose();

    EXPORT_FUNC virtual bool AsyncSend(const Buffer &buffer, int ack = -1);     // 异步发送，不关心返回结果
    EXPORT_FUNC virtual bool AwaitSend(const Buffer &buffer, Buffer &response); // 等待返回结果的发送，关心返回的结果

public:
    EXPORT_FUNC virtual bool TryHandshake(uint32_t timeOutMs);
    EXPORT_FUNC virtual CheckHandshakeStatus CheckHandshakeTryMsg(Buffer &buffer);
    EXPORT_FUNC virtual CheckHandshakeStatus CheckHandshakeConfirmMsg(Buffer &buffer);

private:
    void ProcessPakage();

private:
    std::atomic<int> seq;
    SafeQueue<WebSocketPakage *> _RecvPaks;
    SafeQueue<WebSocketPakage *> _SendPaks;
    SafeMap<int, AwaitTask *> _AwaitMap; // seq->AwaitTask
    Buffer cacheBuffer;                  // 数据帧解析缓冲

    // 握手用
    std::string _SecWsKey;
    std::mutex _tryHandshakeMutex;
    std::condition_variable _tryHandshakeCV;
};