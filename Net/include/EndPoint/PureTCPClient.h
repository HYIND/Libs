#pragma once

#include "TCPEndPoint.h"

// 基于TCP协议的应用层客户端封装
class PureTCPClient : public TCPEndPoint
{
public:
    EXPORT_FUNC PureTCPClient(TCPTransportConnection *con = nullptr);
    EXPORT_FUNC ~PureTCPClient();

public:
    EXPORT_FUNC virtual bool Connect(const std::string &IP, uint16_t Port);
    EXPORT_FUNC virtual bool Release();

    EXPORT_FUNC virtual bool OnRecvBuffer(Buffer *buffer);
    EXPORT_FUNC virtual bool OnConnectClose();

    EXPORT_FUNC virtual bool Send(const Buffer &buffer);

public:
    EXPORT_FUNC virtual bool TryHandshake(uint32_t timeOutMs);
    EXPORT_FUNC virtual CheckHandshakeStatus CheckHandshakeTryMsg(Buffer &buffer);
    EXPORT_FUNC virtual CheckHandshakeStatus CheckHandshakeConfirmMsg(Buffer &buffer);

private:
    Buffer cacheBuffer;
};