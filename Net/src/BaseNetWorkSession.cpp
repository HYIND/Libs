#include "Session/BaseNetWorkSession.h"

BaseNetWorkSession::BaseNetWorkSession()
	: isHandshakeComplete(false)
{
}

BaseNetWorkSession::~BaseNetWorkSession()
{
	isHandshakeComplete = false;
	_callbackRecvData = nullptr;
	_callbackSessionClose = nullptr;
}

Task<bool> BaseNetWorkSession::Connect(std::string IP, uint16_t Port)
{
	Release();

	bool result = co_await BaseClient->Connect(IP, Port);
	if (!result)
		co_return false;

	co_await BaseClient->BindCloseCallBack(std::bind(&BaseNetWorkSession::SessionClose, this, std::placeholders::_1));
	co_await BaseClient->BindMessageCallBack(std::bind(&BaseNetWorkSession::RecvData, this, std::placeholders::_1, std::placeholders::_2));
	if (!co_await TryHandshake())
	{
		std::cout << "BaseNetWorkSession::ConnectAsync TryHandshake Connect Fail! CloseConnection\n";
		Release();
		co_return false;
	}

	co_return true;
}

bool BaseNetWorkSession::Release()
{
	isHandshakeComplete = false;
	_callbackRecvData = nullptr;
	_callbackSessionClose = nullptr;
	return BaseClient->Release();
}

Task<void> BaseNetWorkSession::BindRecvDataCallBack(std::function<Task<void>(BaseNetWorkSession*, Buffer* recv)> callback)
{
	_callbackRecvData = callback;
	co_await OnBindRecvDataCallBack();
}
Task<void> BaseNetWorkSession::BindSessionCloseCallBack(std::function<Task<void>(BaseNetWorkSession*)> callback)
{
	_callbackSessionClose = callback;
	co_await OnBindSessionCloseCallBack();
}

char* BaseNetWorkSession::GetIPAddr()
{
	return BaseClient->GetBaseCon()->GetIPAddr();
}

uint16_t BaseNetWorkSession::GetPort()
{
	return BaseClient->GetBaseCon()->GetPort();
}

void BaseNetWorkSession::SetHandShakeTimeOut(uint32_t ms)
{
	_handshaketimeOutMs = ms;
}
uint32_t BaseNetWorkSession::GetHandShakeTimeOut()
{
	return _handshaketimeOutMs;
}

Task<void>  BaseNetWorkSession::RecvData(TCPEndPoint* client, Buffer* buffer)
{
	if (client != BaseClient)
		co_return;
	co_await OnRecvData(buffer);
}

Task<void>  BaseNetWorkSession::SessionClose(TCPEndPoint* client)
{
	if (client != BaseClient)
		co_return;
	co_await OnSessionClose();
}

TCPEndPoint* BaseNetWorkSession::GetBaseClient()
{
	return BaseClient;
}
