#include "Session/PureWebSocketSession.h"

using Base = BaseNetWorkSession;

PureWebSocketSession::PureWebSocketSession(WebSocketClient* client)
{
	if (client)
		BaseClient = client;
	else
		BaseClient = new WebSocketClient();
}

PureWebSocketSession::~PureWebSocketSession()
{
	Release();
	SAFE_DELETE(BaseClient);
}

Task<bool> PureWebSocketSession::Connect(std::string IP, uint16_t Port)
{
	co_return co_await Base::Connect(IP, Port);
}

bool PureWebSocketSession::Release()
{
	bool result = Base::Release();

	PureWebSocketSessionPakage* pak = nullptr;
	while (_RecvPaks.dequeue(pak))
	{
		_RecvPaks.dequeue(pak);
		SAFE_DELETE(pak);
	}

	return result;
}

bool PureWebSocketSession::AsyncSend(const Buffer& buffer)
{
	return Send(buffer);
}

Task<void> PureWebSocketSession::OnSessionClose()
{
	auto callback = _callbackSessionClose;
	Release();
	if (callback)
		co_await callback(this);
	co_return;
}

Task<void> PureWebSocketSession::OnRecvData(Buffer* buffer)
{
	PureWebSocketSessionPakage* pak = new PureWebSocketSessionPakage();
	pak->buffer.QuoteFromBuf(*buffer);
	_ProcessLock.lock();
	co_await ProcessPakage(pak);
	_ProcessLock.unlock();

	co_return;
}

Task<void> PureWebSocketSession::OnBindRecvDataCallBack()
{
	if (_ProcessLock.try_lock())
	{
		try
		{
			co_await ProcessPakage();
		}
		catch (const std::exception& e)
		{
			_ProcessLock.unlock();
			std::cerr << e.what() << '\n';
		}
		_ProcessLock.unlock();
	}
}

Task<void> PureWebSocketSession::OnBindSessionCloseCallBack()
{
	co_return;
}

Task<bool> PureWebSocketSession::TryHandshake()
{
	co_return true;
}

CheckHandshakeStatus PureWebSocketSession::CheckHandshakeTryMsg(Buffer& buffer)
{
	isHandshakeComplete = true;
	return CheckHandshakeStatus::Success;
}

CheckHandshakeStatus PureWebSocketSession::CheckHandshakeConfirmMsg(Buffer& buffer)
{
	isHandshakeComplete = true;
	return CheckHandshakeStatus::Success;
}

WebSocketClient* PureWebSocketSession::GetBaseClient()
{
	return (WebSocketClient*)BaseClient;
}

bool PureWebSocketSession::Send(const Buffer& buffer)
{
	try
	{
		if (!buffer.Data() || buffer.Length() < 0)
			return true;

		Buffer buf(buffer);
		return BaseClient->Send(buf);
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << '\n';
		return false;
	}
}

Task<void> PureWebSocketSession::ProcessPakage(PureWebSocketSessionPakage* newPak)
{
	if (newPak)
	{
		if (_RecvPaks.size() > 300)
		{
			PureWebSocketSessionPakage* pak = nullptr;
			if (_RecvPaks.dequeue(pak))
				SAFE_DELETE(pak);
		}
		_RecvPaks.enqueue(newPak);
	}

	int count = 10;
	PureWebSocketSessionPakage* pak = nullptr;
	while (_RecvPaks.front(pak) && count > 0)
	{
		auto callback = _callbackRecvData;
		if (callback)
		{
			co_await callback(this, &pak->buffer);
			_RecvPaks.dequeue(pak);
			SAFE_DELETE(pak);
		}
		count--;
	}
}
