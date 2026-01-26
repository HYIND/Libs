#include "Connection/TCPTransportConnection.h"
#include "Core/NetCoredef.h"

using namespace std;

#ifdef __linux__
BaseSocket NewClientSocket(const std::string& IP, uint16_t port, __socket_type protocol, sockaddr_in& sock_addr)
{
	memset(&sock_addr, '0', sizeof(sock_addr));
	sock_addr.sin_family = AF_INET;
	sock_addr.sin_port = htons(port);

	sock_addr.sin_addr.s_addr = inet_addr(IP.c_str());

	return ::socket(PF_INET, protocol, 0);
}

#elif _WIN32
BaseSocket NewClientSocket(const std::string& IP, uint16_t port, int protocol, sockaddr_in& sock_addr)
{

	ZeroMemory(&sock_addr, sizeof(sock_addr));
	sock_addr.sin_family = AF_INET;
	sock_addr.sin_port = htons(port);

	inet_pton(AF_INET, IP.c_str(), &(sock_addr.sin_addr.s_addr));

	return WSASocket(sock_addr.sin_family, protocol, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
}
#endif

TCPTransportConnection::TCPTransportConnection() : BaseTransportConnection(SocketType::TCP, true)
{
}
TCPTransportConnection::~TCPTransportConnection()
{
	Release();
}

bool TCPTransportConnection::Connect(const std::string& IP, uint16_t Port)
{
	if (ValidSocket())
		Release();

	BaseSocket socket = NewClientSocket(IP, Port, _type == SocketType::UDP ? SOCK_DGRAM : SOCK_STREAM, _addr);
	if (socket <= 0)
	{
		perror("Create fd error");
		return false;
	}
	int result = connect(socket, (struct sockaddr*)&_addr, sizeof(struct sockaddr));
	if (result < 0)
	{
		perror("connect socket error");
		return false;
	}
	this->_socket = socket;

	NetCore->AddNetFd(GetBaseShared());
	return true;
}

#ifdef __linux__
Task<bool> TCPTransportConnection::ConnectAsync(const std::string& IP, uint16_t Port)
{
	if (ValidSocket())
		Release();

	int fd = NewClientSocket(IP, Port, _type == SocketType::UDP ? SOCK_DGRAM : SOCK_STREAM, _addr);
	if (fd == -1)
	{
		perror("Create fd error");
		co_return false;
	}

	// int result = connect(fd, (struct sockaddr *)&_addr, sizeof(struct sockaddr));
	int result = co_await CoConnection(fd, _addr);

	if (result < 0)
	{
		perror("connectAsync socket error");
		co_return false;
	}

	this->_socket = fd;

	NetCore->AddNetFd(GetBaseShared());

	co_return true;
}
#endif

void TCPTransportConnection::Apply(const BaseSocket socket, const sockaddr_in& sockaddr, const SocketType type)
{
	if (ValidSocket())
		Release();

	this->_socket = socket;
	this->_addr = sockaddr;
	this->_type = type;
	NetCore->AddNetFd(GetBaseShared());
}

bool TCPTransportConnection::Release()
{
	NetCore->DelNetFd(this);
	bool result = false;
	if (!CloseSocket(_socket))
		result = false;
	else
	{
		// std::cout << std::this_thread::get_id() << " close " << _fd << "\n";
		_socket = Invaild_Socket;
		result = true;
	}

	{
		std::lock_guard<SpinLock> processlock(_ProcessLock);
		_callbackBuffer = nullptr;
		Buffer* buf = nullptr;
		while (_RecvDatas.dequeue(buf))
			SAFE_DELETE(buf);
	}
	{

		std::lock_guard<CriticalSectionLock> sendlock(_SendResMtx);
		Buffer* buf = nullptr;
		while (_SendDatas.dequeue(buf))
			SAFE_DELETE(buf);
	}
	_callbackRDHUP = nullptr;

	return result;
}

bool TCPTransportConnection::Send(const Buffer& buffer)
{
	try
	{
		if (!buffer.Data() || buffer.Length() < 0)
			return true;

		Buffer* buf = new Buffer();
		buf->CopyFromBuf(buffer);
		_SendDatas.enqueue(buf);
		return NetCore->SendRes(GetBaseShared());
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << '\n';
		return false;
	}
}

void TCPTransportConnection::BindBufferCallBack(function<void(TCPTransportConnection*, Buffer*)> callback)
{
	_callbackBuffer = callback;
	OnBindBufferCallBack();
}
void TCPTransportConnection::BindRDHUPCallBack(function<void(TCPTransportConnection*)> callback)
{
	_callbackRDHUP = callback;
	OnBindRDHUPCallBack();
}
SafeQueue<Buffer*>& TCPTransportConnection::GetSendData() { return _SendDatas; }
CriticalSectionLock& TCPTransportConnection::GetSendMtx() { return _SendResMtx; }

#ifdef __linux__
void TCPTransportConnection::OnREAD(BaseSocket socket)
{
	auto read = [&](Buffer& buf, int length) -> bool
		{
			if (buf.Length() < length)
				buf.ReSize(length);

			int remaind = length;
			int trycount = 10;
			while (trycount > 0)
			{
				int result = ::recv(_socket, ((char*)buf.Data()) + (length - remaind), remaind, 0);
				if ((remaind - result) == 0)
				{
					return true;
				}
				if (result <= 0)
				{
					if (result < 0)
					{
						if (errno == EAGAIN || errno == EWOULDBLOCK)
						{
							trycount--;
							continue;
						}
						else if (errno == EINTR)
						{
							trycount--;
							continue;
						}
						else
						{
							buf.ReSize(buf.Length() - remaind);
							return false;
						}
					}
					else
					{
						trycount--;
					}
				}
				remaind -= result;
			}
			buf.ReSize(buf.Length() - remaind);
			return false;
		};

	int recvcount = 10;
	while (recvcount > 0)
	{
		static int MaxBufferLnegth = 2048;
		Buffer* buf = new Buffer(MaxBufferLnegth);
		bool result = read(*buf, MaxBufferLnegth);

		if (buf->Length() > 0)
		{
			_RecvDatas.enqueue(buf);
			if (buf->Length() < MaxBufferLnegth)
			{
				break;
			}
		}
		else
		{
			SAFE_DELETE(buf);
			break;
		}
		recvcount--;
	}

	std::lock_guard<SpinLock> lock(_ProcessLock);
	ProcessRecvQueue();
}
void TCPTransportConnection::OnACCEPT(BaseSocket socket) {}
#endif

void TCPTransportConnection::OnREAD(BaseSocket socket, Buffer& buf)
{

	Buffer* copybuf = new Buffer();
	copybuf->CopyFromBuf(buf);

	_RecvDatas.enqueue(copybuf);

	std::lock_guard<SpinLock> lock(_ProcessLock);
	ProcessRecvQueue();
}

void TCPTransportConnection::OnACCEPT(BaseSocket socket, BaseSocket newsocket, sockaddr_in addr) {}

void TCPTransportConnection::OnBindBufferCallBack()
{
	if (_ProcessLock.trylock())
	{
		try
		{
			ProcessRecvQueue();
		}
		catch (const std::exception& e)
		{
			std::cerr << e.what() << '\n';
		}
		_ProcessLock.unlock();
	}
}

void TCPTransportConnection::OnBindRDHUPCallBack()
{
}

void TCPTransportConnection::ProcessRecvQueue()
{
	while (!_RecvDatas.empty())
	{
		Buffer* buf = nullptr;
		if (!_RecvDatas.front(buf))
			break;

		int pos = buf->Position();
		if (_callbackBuffer)
			_callbackBuffer(this, buf);

		// 该流已经被读取完毕
		if (buf->Length() - buf->Position() == 0)
		{
			_RecvDatas.dequeue(buf);
			if (!ValidSocket())
				return;
			SAFE_DELETE(buf);
			continue;
		}

		// 流未读取完毕，但Postion前后未发生变化，表示应用层暂不需要数据
		if (pos == buf->Position())
			break;
	}
}

void TCPTransportConnection::OnRDHUP()
{
	if (_callbackRDHUP)
		_callbackRDHUP(this);
}
