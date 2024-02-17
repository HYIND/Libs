#include "Core/TcpTransportWarpper.h"
#include "Core/NetCoredef.h"

using namespace std;

#ifdef __linux__
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
#elif _WIN32
SOCKET NewServerSocket(const std::string &IP, uint16_t socket_port, int protocol, sockaddr_in &sock_addr)
{
	memset(&sock_addr, '0', sizeof(sock_addr));
	sock_addr.sin_family = AF_INET;
	sock_addr.sin_port = htons(socket_port);

	inet_pton(AF_INET, IP.c_str(), &(sock_addr.sin_addr.s_addr));

	SOCKET _socket = WSASocket(sock_addr.sin_family, protocol, 0, NULL, 0, WSA_FLAG_OVERLAPPED);

	int result = 0;
	result = ::bind(_socket, (struct sockaddr *)&sock_addr, sizeof(struct sockaddr));
	if (result)
	{
		perror("bind socket error");
		return -1;
	}

	return _socket;
}
#endif
#ifdef __linux__
int NewClientSocket(const std::string &IP, uint16_t socket_port, __socket_type protocol, sockaddr_in &sock_addr)
{
	memset(&sock_addr, '0', sizeof(sock_addr));
	sock_addr.sin_family = AF_INET;
	sock_addr.sin_port = htons(socket_port);

	sock_addr.sin_addr.s_addr = inet_addr(IP.c_str());

	int socket_fd = socket(PF_INET, protocol, 0);

	return socket_fd;
}
#elif _WIN32
SOCKET NewClientSocket(const std::string &IP, uint16_t socket_port, int protocol, sockaddr_in &sock_addr)
{
	memset(&sock_addr, '0', sizeof(sock_addr));
	sock_addr.sin_family = AF_INET;
	sock_addr.sin_port = htons(socket_port);

	inet_pton(AF_INET, IP.c_str(), &(sock_addr.sin_addr.s_addr));

	SOCKET _socket = WSASocket(sock_addr.sin_family, protocol, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	return _socket;
}
#endif

Net::Net(SocketType type, bool isclient) : _type(type), _isclient(isclient)
{
}
#ifdef __linux__
int Net::GetFd()
{
	return this->_fd;
}
#elif _WIN32
SOCKET Net::GetSocket()
{
	return this->_socket;
}
#endif
SocketType Net::GetType()
{
	return this->_type;
}
sockaddr_in Net::GetAddr() { return _addr; }
char *Net::GetIPAddr() { return inet_ntoa(_addr.sin_addr); }
uint16_t Net::GetPort() { return ntohs(_addr.sin_port); }
NetType Net::GetNetType() { return _isclient ? NetType::Client : NetType::Listener; }
bool Net::ValidSocket()
{
#ifdef __linux__
	return this->_fd > 0;
#elif _WIN32
	return this->_socket != INVALID_SOCKET;
#endif
}

TCPNetListener::TCPNetListener() : Net(SocketType::TCP, false)
{
}
TCPNetListener::~TCPNetListener()
{
	if (!ValidSocket())
		return;
	ReleaseListener();
}

bool TCPNetListener::Listen(const string &IP, int Port)
{
	if (ValidSocket())
		ReleaseListener();

#ifdef __linux__
	int fd = NewServerSocket(IP, Port, _type == SocketType::UDP ? SOCK_DGRAM : SOCK_STREAM, _addr);
	if (fd == -1)
	{
		perror("Create fd error");
		return false;
	}
	int ret = listen(fd, 10);
	if (ret < 0)
	{
		perror("listen socket error");
		return false;
	}
	this->_fd = fd;

#elif _WIN32
	SOCKET socket = NewServerSocket(IP, Port, _type == SocketType::UDP ? SOCK_DGRAM : SOCK_STREAM, _addr);
	if (socket == INVALID_SOCKET)
	{
		perror("Create fd error");
		return false;
	}
	int ret = listen(socket, 100);
	if (ret < 0)
	{
		perror("listen socket error");
		return false;
	}
	this->_socket = socket;
#endif

	NetCore->AddNetFd(this);

	return true;
}

bool TCPNetListener::ReleaseListener()
{
#ifdef __linux__
	if (close(_fd) == -1)
		return false;
#elif _WIN32
	if (closesocket(_socket) != 0)
		return false;
#endif

	NetCore->DelNetFd(this);
#ifdef __linux__
	_fd = -1;
#elif _WIN32
	_socket = INVALID_SOCKET;
#endif
	return true;
}

bool TCPNetListener::ReleaseClients()
{
	for (auto it : clients)
	{
		TCPNetClient *client = it.second;
		client->Release();
	}
	return true;
}

void TCPNetListener::BindAcceptCallBack(function<void(TCPNetClient *)> callback)
{
	this->_callbackAccept = callback;
}

#ifdef __linux__
void TCPNetListener::OnEPOLLIN(int fd)
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
				TCPNetClient *client = new TCPNetClient();
				client->Apply(clientFd, addr, this->_type);
				this->clients.insert(pair<int, TCPNetClient *>(clientFd, client));
				cout << "tcpclient connect ,address: " << inet_ntoa(addr.sin_addr) << ":" << ntohs(addr.sin_port) << endl;
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
#elif _WIN32
void TCPNetListener::OnACCEPT(SOCKET socket, sockaddr_in *addr)
{
	if (socket != INVALID_SOCKET)
	{
		TCPNetClient *client = new TCPNetClient();
		client->Apply(socket, *addr, this->_type);
		this->clients.insert(pair<int, TCPNetClient *>(socket, client));
		cout << "client connect ,address: " << inet_ntoa(addr->sin_addr) << ":" << ntohs(addr->sin_port) << endl;
		if (_callbackAccept)
			_callbackAccept(client);
	}
	else
	{
		cout << "socket accept fail!\n";
	}
}
#endif

void TCPNetListener::OnRDHUP()
{
}

TCPNetClient::TCPNetClient() : Net(SocketType::TCP, true)
{
}
TCPNetClient::~TCPNetClient()
{
#ifdef __linux__
	if (_fd <= 0)
#elif _WIN32
	if (_socket == INVALID_SOCKET)
#endif
		return;
	Release();
}

bool TCPNetClient::Connet(const std::string &IP, uint16_t Port)
{
	if (ValidSocket())
		Release();

#ifdef __linux__
	int fd = NewClientSocket(IP, Port, _type == SocketType::UDP ? SOCK_DGRAM : SOCK_STREAM, _addr);
	if (fd == -1)
	{
		perror("Create fd error");
		return false;
	}
	int result = connect(fd, (struct sockaddr *)&_addr, sizeof(struct sockaddr));
	if (result == -1)
	{
		perror("connect socket error");
		return false;
	}
	this->_fd = fd;
#elif _WIN32
	SOCKET socket = NewClientSocket(IP, Port, _type == SocketType::UDP ? SOCK_DGRAM : SOCK_STREAM, _addr);
	if (socket == INVALID_SOCKET)
	{
		perror("Create fd error");
		return false;
	}
	int result = connect(socket, (struct sockaddr *)&_addr, sizeof(struct sockaddr));
	if (result == -1)
	{
		perror("connect socket error");
		return false;
	}

	int flag = 1;
	setsockopt(_socket, IPPROTO_TCP, TCP_NODELAY, (const char *)&flag, sizeof(flag));
	unsigned long ul = 1;
	ioctlsocket(_socket, FIONBIO, &ul);

	this->_socket = socket;
#endif

	NetCore->AddNetFd(this);
	return true;
}
#ifdef __linux__
void TCPNetClient::Apply(const int fd, const sockaddr_in &sockaddr, const SocketType type)
{
	if (ValidSocket())
		Release();

	this->_fd = fd;
	this->_addr = sockaddr;
	this->_type = type;
	NetCore->AddNetFd(this);
}
#elif _WIN32
void TCPNetClient::Apply(const SOCKET socket, const sockaddr_in &sockaddr, const SocketType type)
{
	if (ValidSocket())
		Release();

	this->_socket = socket;
	this->_addr = sockaddr;
	this->_type = type;
	NetCore->AddNetFd(this);
}
#endif

bool TCPNetClient::Release()
{
	bool result = false;
	NetCore->DelNetFd(this);
#ifdef __linux__
	if (close(_fd) == -1)
		result = false;
	_fd = -1;
#elif _WIN32
	if (closesocket(_socket) != 0)
		result = false;
	_socket = INVALID_SOCKET;
#endif
	result = true;

	Buffer *buf = nullptr;
	while (_RecvDatas.dequeue(buf))
		SAFE_DELETE(buf);
	while (_SendDatas.dequeue(buf))
		SAFE_DELETE(buf);

	return result;
}

bool TCPNetClient::Send(const Buffer &buffer)
{
	try
	{
		if (!buffer.Data() || buffer.Length() < 0)
			return true;

		Buffer *buf = new Buffer();
		buf->CopyFromBuf(buffer);
		_SendDatas.enqueue(buf);
		return NetCore->SendRes(this);
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return false;
	}
}

#ifdef __linux__
int TCPNetClient::Read(Buffer &buffer, int length)
{
	Buffer buf(length);
	int result = ::recv(_fd, buf.Data(), length, MSG_NOSIGNAL);
	if (result > 0)
		buffer.QuoteFromBuf(buf);
	return result;
}

#elif _WIN32
int TCPNetClient::Read(Buffer &buffer, int length)
{
	// Buffer buf(length);
	// int result = ::recv(_fd, buf.Data(), length, MSG_NOSIGNAL);
	// if (result > 0)
	//     buffer.QuoteFromBuf(buf);
	// return result;
	return 1;
}
#endif

void TCPNetClient::BindBufferCallBack(function<void(TCPNetClient *, Buffer *)> callback)
{
	_callbackBuffer = callback;
}
void TCPNetClient::BindRDHUPCallBack(function<void(TCPNetClient *)> callback)
{
	_callbackRDHUP = callback;
}
SafeQueue<Buffer *> &TCPNetClient::GetRecvData() { return _RecvDatas; }
SafeQueue<Buffer *> &TCPNetClient::GetSendData() { return _SendDatas; }
std::mutex &TCPNetClient::GetSendMtx() { return _SendResMtx; }

#ifdef __linux__
void TCPNetClient::OnEPOLLIN(int fd)
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
					// if (!this->AsyncSend(HeartBuffer))
					// {
					// 	buf.ReSize(buf.Length() - remaind);
					// 	return false;
					// }
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
			SAFE_DELETE(buf);
			continue;
		}

		// 流未读取完毕，但Postion前后未发生变化，表示应用层暂不需要数据
		if (pos == buf->Postion())
			break;
	}
}
#elif _WIN32
void TCPNetClient::OnREAD(SOCKET socket, Buffer &buffer)
{
	if (socket != this->_socket)
		return;
	Package *pak = nullptr;
	int left = buffer.Length();
	int read = 0;
	while (left > 0)
	{
		if (!_RecvDatas.back(pak) || pak->enable)
		{
			if (_RecvDatas.size() > 300)
			{
				Package *pak = nullptr;
				if (_RecvDatas.dequeue(pak))
					SAFE_DELETE(pak);
			}
			pak = new Package();
			pak->enable = false;
			_RecvDatas.enqueue(pak);
		}

		if (pak->read < sizeof(MsgHeader))
		{
			int min = min(sizeof(MsgHeader) - pak->read, left);
			pak->buffer.Write((char *)(buffer.Data()) + read, min);
			read += min;
			left -= min;
			pak->read += min;
			if (pak->read == sizeof(MsgHeader))
			{
				MsgHeader header;
				memcpy(&header, pak->buffer.Data(), sizeof(MsgHeader));
				pak->seq = header.seq;
				pak->ack = header.ack;
				pak->buffer.ReSize(header.length);
			}
			else
			{
				int i = 1;
				i++;
			}
		}
		else
		{
			int pakBuffeHasRead = pak->read - sizeof(MsgHeader);
			int pakBufferNeedRead = pak->buffer.Length() - pakBuffeHasRead;
			int min = min(pakBufferNeedRead, left);
			pak->buffer.Write((char *)(buffer.Data()) + read, min);
			read += min;
			left -= min;
			pak->read += min;
			if (min == pakBufferNeedRead)
			{
				pak->enable = true;
				if (pak->ack != -1)
				{
					if (_AwaitMap.find(pak->ack) != _AwaitMap.end())
					{
						if (_AwaitMap[pak->ack]->respsonse)
							_AwaitMap[pak->ack]->respsonse->CopyFromBuf(pak->buffer);

						int count = 0;
						while (!_AwaitMap[pak->ack]->isWait && count < 5)
						{
							count++;
							Sleep(10);
						}
						Sleep(5);
						printf("notify_all , pak->ack:%d\n", pak->ack);
						_AwaitMap[pak->ack]->_cv.notify_all();
					}
				}
				else if (_callbackMessage)
				{
					Buffer resposne;
					_callbackMessage(this, pak, &resposne);
					if (resposne.Length() > 0)
						this->AsyncSend(resposne, pak->seq);
					resposne.Release();
				}
			}
			else
			{
				int i = 1;
				i++;
			}
		}
	}
}
#endif

void TCPNetClient::OnRDHUP()
{
	cout << "OnRDHUP" << endl;
	// NetCore->DelNetFd(this);

	if (_callbackRDHUP)
		_callbackRDHUP(this);
}