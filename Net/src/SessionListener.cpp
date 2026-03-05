#include "Session/SessionListener.h"
#include "Session/CustomTcpSession.h"
#include "Session/CustomWebSocketSession.h"
#include "Session/PureWebSocketSession.h"

static int64_t GetTimestampMilliseconds()
{
	auto now = std::chrono::system_clock::now();
	auto duration = now.time_since_epoch();
	return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

constexpr int64_t session_expired_time_ms = 30 * 1000;

struct SessionData
{
	BaseNetWorkSession* session;
	int64_t expiredtime;
	SessionData(BaseNetWorkSession* session) : session(session)
	{
		expiredtime = GetTimestampMilliseconds() + session_expired_time_ms;
	}
};

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
	// 定期检测过期连接
	CleanExpiredTask = TimerTask::CreateRepeat("TcpEndPointListener::CleanExpiredClient",
		30 * 1000,
		std::bind(&NetWorkSessionListener::CleanExpiredSession, this),
		30 * 1000);
	CleanExpiredTask->Run();
}

NetWorkSessionListener::~NetWorkSessionListener()
{
	if (CleanExpiredTask)
	{
		CleanExpiredTask->Clean();
		CleanExpiredTask = nullptr;
	}
}

bool NetWorkSessionListener::Listen(const std::string& IP, int Port)
{
	return BaseListener.Listen(IP, Port);
}

void NetWorkSessionListener::BindSessionEstablishCallBack(std::function<Task<void>(BaseNetWorkSession*)> callback)
{
	_callBackSessionEstablish = callback;
}

Task<void> NetWorkSessionListener::RecvClient(TCPEndPoint* waitClient)
{

	switch (_sessiontype)
	{
	case SessionType::CustomTCPSession:
	{

		PureTCPClient* TCPClient = (PureTCPClient*)waitClient;
		BaseNetWorkSession* session = new CustomTcpSession(TCPClient);
		waitSessions.emplace(std::make_shared<SessionData>(session));
		co_await TCPClient->BindCloseCallBack(std::bind(&NetWorkSessionListener::ClientClose, this, std::placeholders::_1));
		co_await TCPClient->BindMessageCallBack(std::bind(&NetWorkSessionListener::Handshake, this, std::placeholders::_1, std::placeholders::_2));
	}
	break;
	case SessionType::CustomWebSockectSession:
	{
		WebSocketClient* WSClient = (WebSocketClient*)waitClient;
		BaseNetWorkSession* session = new CustomWebSocketSession(WSClient);
		waitSessions.emplace(std::make_shared<SessionData>(session));
		co_await WSClient->BindCloseCallBack(std::bind(&NetWorkSessionListener::ClientClose, this, std::placeholders::_1));
		co_await WSClient->BindMessageCallBack(std::bind(&NetWorkSessionListener::Handshake, this, std::placeholders::_1, std::placeholders::_2));
	}
	break;
	case SessionType::PureWebSocketSession:
	{
		WebSocketClient* WSClient = (WebSocketClient*)waitClient;
		BaseNetWorkSession* session = new PureWebSocketSession(WSClient);
		waitSessions.emplace(std::make_shared<SessionData>(session));
		co_await WSClient->BindCloseCallBack(std::bind(&NetWorkSessionListener::ClientClose, this, std::placeholders::_1));
		co_await WSClient->BindMessageCallBack(std::bind(&NetWorkSessionListener::Handshake, this, std::placeholders::_1, std::placeholders::_2));
	}
	break;
	default:
		std::cout << "SessionListener: SessionType Error!\n";
		break;
	}
	co_return;
}

Task<void> NetWorkSessionListener::ClientClose(TCPEndPoint* client)
{
	waitSessions.EnsureCall(
		[&](std::vector<std::shared_ptr<SessionData>>& array) -> void
		{
			for (auto it = array.begin(); it != array.end();)
			{
				std::shared_ptr<SessionData> sessiondata = *it;
				BaseNetWorkSession* session = sessiondata->session;
				TCPEndPoint* base = session->GetBaseClient();
				if (base != client)
				{
					it++;
					continue;
				}
				else
				{
					session->Release();
					array.erase(it);
					DeleteLater(session);
					return;
				}
			}
		});
	co_return;
}

Task<void> NetWorkSessionListener::Handshake(TCPEndPoint* waitClient, Buffer* buf)
{
	waitSessions.EnsureCall(
		[&](std::vector<std::shared_ptr<SessionData>>& array) -> void
		{
			for (auto it = array.begin(); it != array.end();)
			{
				std::shared_ptr<SessionData> sessiondata = *it;
				BaseNetWorkSession* session = sessiondata->session;
				TCPEndPoint* base = session->GetBaseClient();
				if (base != waitClient)
				{
					it++;
					continue;
				}

				else
				{

					CheckHandshakeStatus result = session->CheckHandshakeTryMsg(*buf); // 由Listener负责调用具体的Client握手方法，并监听请结果
					if (result == CheckHandshakeStatus::Success)
					{
						if (_callBackSessionEstablish)
						 	_callBackSessionEstablish(session).sync_wait();
						array.erase(it);
						waitClient->BindCloseCallBack(std::bind(&BaseNetWorkSession::SessionClose, session, std::placeholders::_1)).sync_wait();
						waitClient->BindMessageCallBack(std::bind(&BaseNetWorkSession::RecvData, session, std::placeholders::_1, std::placeholders::_2)).sync_wait();
						if (buf->Remain() > 0)
							session->RecvData(base, buf).sync_wait();
					}
					if (result == CheckHandshakeStatus::BufferAgain)
					{
					}
					if (result == CheckHandshakeStatus::Fail)
					{
						session->Release();
						array.erase(it);
						DeleteLater(session);
					}
					return;
				}
			}
		});
	co_return;
}
void NetWorkSessionListener::CleanExpiredSession()
{
	int64_t timestamp = GetTimestampMilliseconds();
	waitSessions.EnsureCall(
		[&](std::vector<std::shared_ptr<SessionData>>& array) -> void
		{
			for (auto it = array.begin(); it != array.end();)
			{
				std::shared_ptr<SessionData> sessiondata = *it;
				BaseNetWorkSession* session = sessiondata->session;
				if (timestamp > sessiondata->expiredtime)
				{
					session->Release();
					it = array.erase(it);
					DeleteLater(session);
				}
				else
				{
					it++;
				}
			}
		});
}