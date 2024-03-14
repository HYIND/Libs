#include "Session/SessionListener.h"
#include "Session/CustomTcpSession.h"
#include "Session/CustomWebSocketSession.h"
#include "Session/PureWebSocketSession.h"

NetWorkSessionListener::NetWorkSessionListener(SessionType type)
    : _sessiontype(type)
{
    switch (_sessiontype)
    {
    case SessionType::CustomTCPSession:
        BaseListener.SetProtocol(TCPNetProtocol::PureTCP);
        break;
    case SessionType::CustomWebSockectSession:
    case SessionType::PureWebSocketSession:
        BaseListener.SetProtocol(TCPNetProtocol::WebSocket);
        break;
    default:
        std::cout << "SessionListener: SessionType Error!\n";
        break;
    }
    BaseListener.BindEstablishConnectionCallBack(std::bind(&NetWorkSessionListener::RecvClient, this, std::placeholders::_1));
}

NetWorkSessionListener::~NetWorkSessionListener()
{
}

bool NetWorkSessionListener::Listen(const std::string &IP, int Port)
{
    return BaseListener.Listen(IP, Port);
}

void NetWorkSessionListener::BindSessionEstablishCallBack(std::function<void(BaseNetWorkSession *)> callback)
{
    _callBackSessionEstablish = callback;
}

void NetWorkSessionListener::RecvClient(TCPEndPoint *waitClient)
{

    switch (_sessiontype)
    {
    case SessionType::CustomTCPSession:
    {

        PureTCPClient *TCPClient = (PureTCPClient *)waitClient;
        BaseNetWorkSession *session = new CustomTcpSession(TCPClient);
        waitSessions.emplace(session);
        TCPClient->BindMessageCallBack(std::bind(&NetWorkSessionListener::Handshake, this, std::placeholders::_1, std::placeholders::_2));
    }
    break;
    case SessionType::CustomWebSockectSession:
    {
        WebSocketClient *WSClient = (WebSocketClient *)waitClient;
        BaseNetWorkSession *session = new CustomWebSocketSession(WSClient);
        waitSessions.emplace(session);
        WSClient->BindMessageCallBack(std::bind(&NetWorkSessionListener::Handshake, this, std::placeholders::_1, std::placeholders::_2));
    }
    break;
    case SessionType::PureWebSocketSession:
    {
        WebSocketClient *WSClient = (WebSocketClient *)waitClient;
        BaseNetWorkSession *session = new PureWebSocketSession(WSClient);
        waitSessions.emplace(session);
        WSClient->BindMessageCallBack(std::bind(&NetWorkSessionListener::Handshake, this, std::placeholders::_1, std::placeholders::_2));
    }
    break;
    default:
    {
        std::cout << "SessionListener: SessionType Error!\n";
        SAFE_DELETE(waitClient);
    }
    break;
    }
}

void NetWorkSessionListener::Handshake(TCPEndPoint *waitClient, Buffer *buf)
{
    waitSessions.EnsureCall(
        [&](std::vector<BaseNetWorkSession *> &array) -> void
        {
            for (auto it = array.begin(); it != array.end(); it++)
            {
                BaseNetWorkSession *session = *it;
                TCPEndPoint *base = session->GetBaseClient();
                if (base != waitClient)
                    continue;

                else
                {

                    CheckHandshakeStatus result = session->CheckHandshakeTryMsg(*buf); // 由Listener负责调用具体的Client握手方法，并监听请结果
                    if (result == CheckHandshakeStatus::Success)
                    {
                        if (_callBackSessionEstablish)
                            _callBackSessionEstablish(session);
                        array.erase(it);
                        waitClient->BindMessageCallBack(std::bind(&BaseNetWorkSession::RecvData, session, std::placeholders::_1, std::placeholders::_2));
                        waitClient->BindCloseCallBack(std::bind(&BaseNetWorkSession::SessionClose, session, std::placeholders::_1));
                        if (buf->Remaind() > 0)
                            session->RecvData(base, buf);
                    }
                    if (result == CheckHandshakeStatus::BufferAgain)
                    {
                    }
                    if (result == CheckHandshakeStatus::Fail)
                    {
                        session->Release();
                        array.erase(it);
                    }
                    return;
                }
            }
        });
}