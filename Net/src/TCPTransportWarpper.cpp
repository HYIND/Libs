#include "Core/TCPTransportWarpper.h"
#include "Core/NetCoredef.h"

using namespace std;

int NewServerSocket(const std::string &IP, uint16_t socket_port, __socket_type protocol, sockaddr_in &sock_addr)
{
	memset(&sock_addr, '0', sizeof(sock_addr));
	sock_addr.sin_family = AF_INET;
	sock_addr.sin_port = htons(socket_port);

	sock_addr.sin_addr.s_addr = inet_addr(IP.c_str());

	int socket_fd = socket(PF_INET, protocol, 0);

	int result = 0;
	result = bind(socket_fd, (struct sockaddr *)&sock_addr, sizeof(struct sockaddr));
	if (result)
	{
		perror("bind socket error");
		return -1;
	}

	return socket_fd;
}

int NewClientSocket(const std::string &IP, uint16_t socket_port, __socket_type protocol, sockaddr_in &sock_addr)
{
	memset(&sock_addr, '0', sizeof(sock_addr));
	sock_addr.sin_family = AF_INET;
	sock_addr.sin_port = htons(socket_port);

	sock_addr.sin_addr.s_addr = inet_addr(IP.c_str());

	int socket_fd = socket(PF_INET, protocol, 0);

	return socket_fd;
}

BaseTransportConnection::BaseTransportConnection(SocketType type, bool isclient) : _type(type), _isclient(isclient)
{
}
std::shared_ptr<BaseTransportConnection> BaseTransportConnection::GetBaseShared()
{
	return shared_from_this();
}

int BaseTransportConnection::GetFd()
{
	return this->_fd;
}

SocketType BaseTransportConnection::GetType()
{
	return this->_type;
}
sockaddr_in BaseTransportConnection::GetAddr() { return _addr; }
char *BaseTransportConnection::GetIPAddr() { return inet_ntoa(_addr.sin_addr); }
uint16_t BaseTransportConnection::GetPort() { return ntohs(_addr.sin_port); }
NetType BaseTransportConnection::GetNetType() { return _isclient ? NetType::Client : NetType::Listener; }
bool BaseTransportConnection::ValidSocket()
{
	return this->_fd > 0;
}

TCPTransportListener::TCPTransportListener() : BaseTransportConnection(SocketType::TCP, false)
{
}
TCPTransportListener::~TCPTransportListener()
{
	if (!ValidSocket())
		return;
	ReleaseListener();
}

bool TCPTransportListener::Listen(const string &IP, int Port)
{
	if (ValidSocket())
		ReleaseListener();

	int fd = NewServerSocket(IP, Port, _type == SocketType::UDP ? SOCK_DGRAM : SOCK_STREAM, _addr);
	if (fd == -1)
	{
		perror("Create fd error");
		return false;
	}
	int ret = listen(fd, 5000);
	if (ret < 0)
	{
		perror("listen socket error");
		return false;
	}
	this->_fd = fd;

	NetCore->AddNetFd(GetBaseShared());

	return true;
}

bool TCPTransportListener::ReleaseListener()
{
	if (close(_fd) == -1)
		return false;

	NetCore->DelNetFd(this);
	_fd = -1;
	return true;
}

bool TCPTransportListener::ReleaseClients()
{
	return true;
}

void TCPTransportListener::BindAcceptCallBack(std::function<void(std::shared_ptr<TCPTransportConnection>)> callback)
{
	this->_callbackAccept = callback;
}

void TCPTransportListener::OnREAD(int fd)
{
	OnACCEPT(fd);
}
void TCPTransportListener::OnREAD(int fd, Buffer &Buffer)
{
	OnACCEPT(fd);
}

void TCPTransportListener::OnACCEPT(int fd)
{
	if (fd == this->_fd)
	{
		while (true)
		{
			sockaddr_in addr;
			socklen_t length = sizeof(sockaddr_in);
			int clientFd = accept(this->_fd, (struct sockaddr *)&addr, &length);
			if (clientFd != -1)
			{
				std::shared_ptr<TCPTransportConnection> client = std::make_shared<TCPTransportConnection>();
				client->Apply(clientFd, addr, this->_type);
				// cout << "tcpclient connect ,address: " << inet_ntoa(addr.sin_addr) << ":" << ntohs(addr.sin_port) << endl;
				if (_callbackAccept)
					_callbackAccept(client);
			}
			else
			{
				// cout << "socket accept fail!\n";
				break;
			}
		}
	}
}
void TCPTransportListener::OnACCEPT(int fd, int newclient, sockaddr_in addr)
{
	if (fd == this->_fd)
	{
		if (newclient < 0)
			return;

		int clientFd = newclient;
		std::shared_ptr<TCPTransportConnection> client = std::make_shared<TCPTransportConnection>();
		client->Apply(clientFd, addr, this->_type);
		// cout << "tcpclient connect ,address: " << inet_ntoa(addr.sin_addr) << ":" << ntohs(addr.sin_port) << endl;
		if (_callbackAccept)
			_callbackAccept(client);
	}
}

void TCPTransportListener::OnRDHUP()
{
}

TCPTransportConnection::TCPTransportConnection() : BaseTransportConnection(SocketType::TCP, true)
{
}
TCPTransportConnection::~TCPTransportConnection()
{
	Release();
}

bool TCPTransportConnection::Connect(const std::string &IP, uint16_t Port)
{
	if (ValidSocket())
		Release();

	int fd = NewClientSocket(IP, Port, _type == SocketType::UDP ? SOCK_DGRAM : SOCK_STREAM, _addr);
	if (fd == -1)
	{
		perror("Create fd error");
		return false;
	}
	int result = connect(fd, (struct sockaddr *)&_addr, sizeof(struct sockaddr));
	if (result < 0)
	{
		perror("connect socket error");
		return false;
	}
	this->_fd = fd;

	NetCore->AddNetFd(GetBaseShared());
	return true;
}

Task<bool> TCPTransportConnection::ConnectAsync(const std::string &IP, uint16_t Port)
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

	this->_fd = fd;

	NetCore->AddNetFd(GetBaseShared());

	co_return true;
}

void TCPTransportConnection::Apply(const int fd, const sockaddr_in &sockaddr, const SocketType type)
{
	if (ValidSocket())
		Release();

	this->_fd = fd;
	this->_addr = sockaddr;
	this->_type = type;
	NetCore->AddNetFd(GetBaseShared());
}

bool TCPTransportConnection::Release()
{
	NetCore->DelNetFd(this);
	bool result = false;
	if (close(_fd) == -1)
		result = false;
	else
	{
		// std::cout << std::this_thread::get_id() << " close " << _fd << "\n";
		_fd = -1;
		result = true;
	}

	Buffer *buf = nullptr;
	while (_RecvDatas.dequeue(buf))
		SAFE_DELETE(buf);
	std::lock_guard<CriticalSectionLock> lock(_SendResMtx);
	while (_SendDatas.dequeue(buf))
		SAFE_DELETE(buf);

	return result;
}

bool TCPTransportConnection::Send(const Buffer &buffer)
{
	try
	{
		if (!buffer.Data() || buffer.Length() < 0)
			return true;

		Buffer *buf = new Buffer();
		buf->CopyFromBuf(buffer);
		_SendDatas.enqueue(buf);
		return NetCore->SendRes(GetBaseShared());
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return false;
	}
}

int TCPTransportConnection::Read(Buffer &buffer, int length)
{
	Buffer buf(length);
	int result = ::recv(_fd, buf.Data(), length, MSG_NOSIGNAL);
	if (result > 0)
		buffer.QuoteFromBuf(buf);
	return result;
}

void TCPTransportConnection::BindBufferCallBack(function<void(TCPTransportConnection *, Buffer *)> callback)
{
	_callbackBuffer = callback;
	OnBindBufferCallBack();
}
void TCPTransportConnection::BindRDHUPCallBack(function<void(TCPTransportConnection *)> callback)
{
	_callbackRDHUP = callback;
	OnBindRDHUPCallBack();
}
SafeQueue<Buffer *> &TCPTransportConnection::GetRecvData() { return _RecvDatas; }
SafeQueue<Buffer *> &TCPTransportConnection::GetSendData() { return _SendDatas; }
CriticalSectionLock &TCPTransportConnection::GetSendMtx() { return _SendResMtx; }

void TCPTransportConnection::OnREAD(int fd)
{
	auto read = [&](Buffer &buf, int length) -> bool
	{
		if (buf.Length() < length)
			buf.ReSize(length);

		int remaind = length;
		int trycount = 10;
		while (trycount > 0)
		{
			int result = ::recv(_fd, ((char *)buf.Data()) + (length - remaind), remaind, 0);
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
		Buffer *buf = new Buffer(MaxBufferLnegth);
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
void TCPTransportConnection::OnREAD(int fd, Buffer &buf)
{

	Buffer *copybuf = new Buffer();
	copybuf->CopyFromBuf(buf);

	_RecvDatas.enqueue(copybuf);

	std::lock_guard<SpinLock> lock(_ProcessLock);
	ProcessRecvQueue();
}

void TCPTransportConnection::OnACCEPT(int fd) {}
void TCPTransportConnection::OnACCEPT(int fd, int newclient, sockaddr_in addr) {}

void TCPTransportConnection::OnBindBufferCallBack()
{
	if (_ProcessLock.trylock())
	{
		try
		{
			ProcessRecvQueue();
		}
		catch (const std::exception &e)
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
		Buffer *buf = nullptr;
		if (!_RecvDatas.front(buf))
			break;

		int pos = buf->Postion();
		if (_callbackBuffer)
			_callbackBuffer(this, buf);

		// 该流已经被读取完毕
		if (buf->Length() - buf->Postion() == 0)
		{
			_RecvDatas.dequeue(buf);
			if (!ValidSocket())
				return;
			SAFE_DELETE(buf);
			continue;
		}

		// 流未读取完毕，但Postion前后未发生变化，表示应用层暂不需要数据
		if (pos == buf->Postion())
			break;
	}
}

void TCPTransportConnection::OnRDHUP()
{
	if (_callbackRDHUP)
		_callbackRDHUP(this);
}