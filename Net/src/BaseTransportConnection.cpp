#include "Connection/BaseTransportConnection.h"
#include "Core/NetCoredef.h"

using namespace std;

BaseTransportConnection::BaseTransportConnection(SocketType type, bool isclient)
	: _type(type), _socket(Invaild_Socket), _isclient(isclient), _OnRDHUPCount(0), _OnREADCount(0), _OnACCEPTCount(0)
{
}
std::shared_ptr<BaseTransportConnection> BaseTransportConnection::GetBaseShared()
{
	return shared_from_this();
}

BaseSocket BaseTransportConnection::GetSocket()
{
	return this->_socket;
}

SocketType BaseTransportConnection::GetType()
{
	return this->_type;
}
sockaddr_in BaseTransportConnection::GetAddr() { return _addr; }
char* BaseTransportConnection::GetIPAddr() { return inet_ntoa(_addr.sin_addr); }
uint16_t BaseTransportConnection::GetPort() { return ntohs(_addr.sin_port); }
NetType BaseTransportConnection::GetNetType() { return _isclient ? NetType::Client : NetType::Listener; }
bool BaseTransportConnection::ValidSocket()
{
	return this->_socket > 0;
}

bool BaseTransportConnection::isOnCallback()
{
	return _OnRDHUPCount.load(std::memory_order_relaxed) > 0 ||
		_OnREADCount.load(std::memory_order_relaxed) > 0 ||
		_OnACCEPTCount.load(std::memory_order_relaxed) > 0;
}

void BaseTransportConnection::RDHUP()
{
	_OnRDHUPCount.fetch_add(1, std::memory_order_relaxed);
	try
	{
		OnRDHUP();
	}
	catch (const std::exception& e)
	{
	}
	_OnRDHUPCount.fetch_sub(1, std::memory_order_relaxed);
}
#ifdef _linux_
void BaseTransportConnection::READ(BaseSocket socket)
{
	_OnREADCount.fetch_add(1, std::memory_order_relaxed);
	try
	{
		OnREAD(socket);
	}
	catch (const std::exception& e)
	{
	}
	_OnREADCount.fetch_sub(1, std::memory_order_relaxed);
}
void BaseTransportConnection::ACCEPT(BaseSocket fdsocket)
{
	_OnACCEPTCount.fetch_add(1, std::memory_order_relaxed);
	try
	{
		OnACCEPT(socket);
	}
	catch (const std::exception& e)
	{
	}
	_OnACCEPTCount.fetch_sub(1, std::memory_order_relaxed);
}
#endif
void BaseTransportConnection::READ(BaseSocket socket, Buffer& buf)
{
	_OnREADCount.fetch_add(1, std::memory_order_relaxed);
	try
	{
		OnREAD(socket, buf);
	}
	catch (const std::exception& e)
	{
	}
	_OnREADCount.fetch_sub(1, std::memory_order_relaxed);
}
void BaseTransportConnection::ACCEPT(BaseSocket socket, BaseSocket newclient, sockaddr_in addr)
{
	_OnACCEPTCount.fetch_add(1, std::memory_order_relaxed);
	try
	{
		OnACCEPT(socket, newclient, addr);
	}
	catch (const std::exception& e)
	{
	}
	_OnACCEPTCount.fetch_sub(1, std::memory_order_relaxed);
}
