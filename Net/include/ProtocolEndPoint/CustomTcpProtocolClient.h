#pragma once

#include "TCPProtocolEndPoint.h"

struct CustomPackage
{
    bool enable = true;
    int read = 0;

    int seq = 0;
    int ack = -1;
    int written = 0;
    Buffer buffer;
};

// 基于自定义应用层协议的客户端
class CustomTCPProtocolClient : public TCPProtocolClient
{
public:
    EXPORT_FUNC CustomTCPProtocolClient(TCPNetClient *con = nullptr);
    EXPORT_FUNC ~CustomTCPProtocolClient();

public:
    EXPORT_FUNC virtual bool Connet(const std::string &IP, uint16_t Port);
    EXPORT_FUNC virtual bool Release();

    EXPORT_FUNC virtual bool OnRecvBuffer(Buffer *buffer);
    EXPORT_FUNC virtual bool OnConnectClose();

    EXPORT_FUNC virtual bool AsyncSend(const Buffer &buffer, int ack = -1);     // 异步发送，不关心返回结果
    EXPORT_FUNC virtual bool AwaitSend(const Buffer &buffer, Buffer &response); // 等待返回结果的发送，关心返回的结果

public:
    EXPORT_FUNC virtual bool TryHandshake(uint32_t timeOutMs);
    EXPORT_FUNC virtual CheckHandshakeStatus CheckHandshakeTryMsg(Buffer &buffer);
    EXPORT_FUNC virtual CheckHandshakeStatus CheckHandshakeConfirmMsg(Buffer &buffer);

private:
    std::atomic<int> seq;
    SafeQueue<CustomPackage *> _RecvPaks;
    SafeQueue<CustomPackage *> _SendPaks;
    SafeMap<int, AwaitTask *> _AwaitMap; // seq->AwaitTask

    // 主动握手用
    std::mutex _tryHandshakeMutex;
    std::condition_variable _tryHandshakeCV;
};