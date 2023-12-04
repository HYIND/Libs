#include "NetWarpper.h"
#include "NetCoredef.h"

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

NetListener::NetListener(SocketType type) : Net(type, false)
{
}
NetListener::~NetListener()
{
	if (!ValidSocket())
		return;
	ReleaseListener();
}

bool NetListener::Listen(const string &IP, int Port)
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

bool NetListener::ReleaseListener()
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

bool NetListener::ReleaseClients()
{
	for (auto it : clients)
	{
		NetClient *client = it.second;
		client->Release();
	}
	return true;
}

void NetListener::BindAcceptCallBack(function<void(NetClient *)> callback)
{
	this->_callbackAccept = callback;
}

#ifdef __linux__
void NetListener::OnEPOLLIN(int fd)
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
				NetClient *client = new NetClient();
				client->Apply(clientFd, addr, this->_type);
				this->clients.insert(pair<int, NetClient *>(clientFd, client));
				cout << "client connect ,address: " << inet_ntoa(addr.sin_addr) << ":" << ntohs(addr.sin_port) << endl;
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
void NetListener::OnACCEPT(SOCKET socket, sockaddr_in *addr)
{
	if (socket != INVALID_SOCKET)
	{
		NetClient *client = new NetClient();
		client->Apply(socket, *addr, this->_type);
		this->clients.insert(pair<int, NetClient *>(socket, client));
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

void NetListener::OnRDHUP()
{
}

NetClient::NetClient(SocketType type) : Net(type, true)
{
	seq = 0;
}
NetClient::~NetClient()
{
#ifdef __linux__
	if (_fd <= 0)
#elif _WIN32
	if (_socket == INVALID_SOCKET)
#endif
		return;
	Release();
}

bool NetClient::Connet(const std::string &IP, uint16_t Port)
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
void NetClient::Apply(const int fd, const sockaddr_in &sockaddr, const SocketType type)
{
	if (ValidSocket())
		Release();

	this->_fd = fd;
	this->_addr = sockaddr;
	this->_type = type;
	NetCore->AddNetFd(this);
}
#elif _WIN32
void NetClient::Apply(const SOCKET socket, const sockaddr_in &sockaddr, const SocketType type)
{
	if (ValidSocket())
		Release();

	this->_socket = socket;
	this->_addr = sockaddr;
	this->_type = type;
	NetCore->AddNetFd(this);
}
#endif

bool NetClient::Release()
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

	Package *pak = nullptr;
	while (_RecvDatas.dequeue(pak))
		SAFE_DELETE(pak);

	return result;
}

bool NetClient::AsyncSend(const Buffer &buffer, int ack)
{
	try
	{
		if (!buffer.Data() || buffer.Length() < 0)
			return true;
		MsgHeader header;
		int seq = this->seq++;
		header.seq = seq;
		header.ack = ack;
		header.length = buffer.Length();
		Buffer buf(sizeof(MsgHeader) + buffer.Length());
		buf.Write(&header, sizeof(MsgHeader));
		buf.Write(buffer);

		Package *pak = new Package();
		pak->seq = header.seq;
		pak->ack = header.ack;
		pak->buffer.QuoteFromBuf(buf);
		this->_SendDatas.enqueue(pak);
		return NetCore->SendRes(this);
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return false;
	}
}

bool NetClient::AwaitSend(const Buffer &buffer, Buffer &response) // 等待返回结果的发送，关心返回的结果
{
	try
	{
		if (!buffer.Data() || buffer.Length() < 0)
			return true;

		bool result = false;
		AwaitTask *task = new AwaitTask();
		task->respsonse = &response;

		MsgHeader header;
		int seq = this->seq++;
		header.seq = seq;
		header.ack = -1;
		header.length = buffer.Length();
		Buffer buf = Buffer(sizeof(MsgHeader) + buffer.Length());
		buf.Write(&header, sizeof(MsgHeader));
		buf.Write(buffer);

		task->seq = header.seq;
		_AwaitMap.insert(pair<int, AwaitTask *>(task->seq, task));

		Package *pak = new Package();
		pak->seq = header.seq;
		pak->ack = header.ack;
		pak->buffer.QuoteFromBuf(buf);
		this->_SendDatas.enqueue(pak);

		if (NetCore->SendRes(this)) // 发送
		{
			unique_lock<mutex> awaitlck(task->_mtx);
			task->isWait = true;
			printf("wait_for , task->seq:%d\n", task->seq);
			result = task->_cv.wait_for(awaitlck, std::chrono::seconds(8)) != std::cv_status::timeout; // 等待返回并超时检查
		}

		auto it = _AwaitMap.find(task->seq);
		if (it != _AwaitMap.end())
			_AwaitMap.erase(it);
		delete task;
		return result;
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return false;
	}
}

#ifdef __linux__
int NetClient::Recv(Buffer &buffer, int length)
{
	Buffer buf(length);
	int result = ::recv(_fd, buf.Data(), length, MSG_NOSIGNAL);
	if (result > 0)
		buffer.QuoteFromBuf(buf);
	return result;
}

#elif _WIN32
int NetClient::Recv(Buffer &buffer, int length)
{
	// Buffer buf(length);
	// int result = ::recv(_fd, buf.Data(), length, MSG_NOSIGNAL);
	// if (result > 0)
	//     buffer.QuoteFromBuf(buf);
	// return result;
	return 1;
}
#endif

void NetClient::BindMessageCallBack(function<void(NetClient *, Package *pak, Buffer *AckResponse)> callback)
{
	this->_callbackMessage = callback;
}
void NetClient::BindRDHUPCallBack(function<void(NetClient *)> callback)
{
	this->_callbackRDHUP = callback;
}
SafeQueue<Package *> &NetClient::GetRecvData() { return _RecvDatas; }
SafeQueue<Package *> &NetClient::GetSendData() { return _SendDatas; }
std::mutex &NetClient::GetSendMtx() { return _SendResMtx; }

#ifdef __linux__
void NetClient::OnEPOLLIN(int fd)
{
	int count = 10;
	while (count > 0)
	{
		auto read = [&](void *dest, int length) -> bool
		{
			int num = length;
			while (true)
			{
				int result = ::recv(_fd, ((char *)dest) + (length - num), num, 0);
				if ((num - result) == 0)
				{
					return true;
				}
				if (result <= 0)
				{
					if (result < 0)
					{
						if (errno == EAGAIN || errno == EWOULDBLOCK)
						{
							continue;
						}
						else if (errno == EINTR)
						{
							continue;
						}
						else
						{
							return false;
						}
					}
					else
					{
						if (!this->AsyncSend(HeartBuffer))
							return false;
					}
				}
				num -= result;
			}
			return false;
		};

		MsgHeader header;
		if (!read(&header, sizeof(MsgHeader)))
		{
			break;
		}
		Buffer buf(header.length);
		read(buf.Data(), header.length);

		if (IsHeartBeat(buf))
			continue;

		Package *pak = new Package();
		pak->seq = header.seq;
		pak->ack = header.ack;
		pak->buffer.QuoteFromBuf(buf);

		if (_RecvDatas.size() > 300)
		{
			Package *pak = nullptr;
			if (_RecvDatas.dequeue(pak))
				SAFE_DELETE(pak);
		}
		_RecvDatas.enqueue(pak);

		if (pak->ack != -1)
		{
			if (_AwaitMap.find(pak->ack) != _AwaitMap.end())
			{
				if (_AwaitMap[pak->ack]->respsonse)
					_AwaitMap[pak->ack]->respsonse->CopyFromBuf(pak->buffer);
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
		count--;
	}
}
#elif _WIN32
void NetClient::OnREAD(SOCKET socket, Buffer &buffer)
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

void NetClient::OnRDHUP()
{
	cout << "OnRDHUP" << endl;
	// NetCore->DelNetFd(this);

	if (_callbackRDHUP)
		_callbackRDHUP(this);
}