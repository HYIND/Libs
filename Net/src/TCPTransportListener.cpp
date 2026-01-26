#include "Connection/TCPTransportListener.h"
#include "Core/NetCoredef.h"

using namespace std;

#ifdef __linux__
BaseSocket NewServerSocket(const std::string& IP, uint16_t port, __socket_type protocol, sockaddr_in& sock_addr)
{
	memset(&sock_addr, '0', sizeof(sock_addr));
	sock_addr.sin_family = AF_INET;
	sock_addr.sin_port = htons(port);

	sock_addr.sin_addr.s_addr = inet_addr(IP.c_str());

	BaseSocket socket = ::socket(PF_INET, protocol, 0);

	int result = 0;
	result = ::bind(socket, (struct sockaddr*)&sock_addr, sizeof(struct sockaddr));
	if (result)
	{
		perror("bind socket error");
		return Invaild_Socket;
	}

	return socket;
}
#elif _WIN32
BaseSocket NewServerSocket(const std::string& IP, uint16_t port, int protocol, sockaddr_in& sock_addr)
{

	ZeroMemory(&sock_addr, sizeof(sock_addr));
	sock_addr.sin_family = AF_INET;
	sock_addr.sin_port = htons(port);

	inet_pton(AF_INET, IP.c_str(), &(sock_addr.sin_addr.s_addr));

	BaseSocket socket = WSASocket(sock_addr.sin_family, protocol, 0, NULL, 0, WSA_FLAG_OVERLAPPED);

	int result = 0;
	result = ::bind(socket, (struct sockaddr*)&sock_addr, sizeof(struct sockaddr));
	if (result)
	{
		perror("bind socket error");
		return Invaild_Socket;
	}

	return socket;
}
#endif

TCPTransportListener::TCPTransportListener() : BaseTransportConnection(SocketType::TCP, false)
{
}
TCPTransportListener::~TCPTransportListener()
{
	if (!ValidSocket())
		return;
	ReleaseListener();
}

bool TCPTransportListener::Listen(const string& IP, int Port)
{
	if (ValidSocket())
		ReleaseListener();

	BaseSocket socket = NewServerSocket(IP, Port, _type == SocketType::UDP ? SOCK_DGRAM : SOCK_STREAM, _addr);
	if (socket == Invaild_Socket)
	{
		perror("Create fd error");
		return false;
	}
	int ret = ::listen(socket, 5000);
	if (ret < 0)
	{
		perror("listen socket error");
		return false;
	}
	this->_socket = socket;

	NetCore->AddNetFd(GetBaseShared());

	return true;
}

bool TCPTransportListener::ReleaseListener()
{
	if (!CloseSocket(_socket))
		return false;

	NetCore->DelNetFd(this);
	_socket = Invaild_Socket;
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

#ifdef __linux__
void TCPTransportListener::OnREAD(BaseSocket socket)
{
}

void TCPTransportListener::OnACCEPT(BaseSocket socket)
{
	if (socket == this->_socket)
	{
		while (true)
		{
			sockaddr_in addr;
			socklen_t length = sizeof(sockaddr_in);
			BaseSocket clientSocket = accept(this->_socket, (struct sockaddr*)&addr, &length);
			if (clientSocket != Invaild_Socket)
			{
				std::shared_ptr<TCPTransportConnection> client = std::make_shared<TCPTransportConnection>();
				client->Apply(clientSocket, addr, this->_type);
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
#endif

void TCPTransportListener::OnREAD(BaseSocket socket, Buffer& Buffer)
{
}

void TCPTransportListener::OnACCEPT(BaseSocket socket, BaseSocket newsocket, sockaddr_in addr)
{
	if (socket == this->_socket)
	{
		if (newsocket <= 0)
			return;

		BaseSocket clientSocket = newsocket;
		std::shared_ptr<TCPTransportConnection> client = std::make_shared<TCPTransportConnection>();
		client->Apply(clientSocket, addr, this->_type);
		// cout << "tcpclient connect ,address: " << inet_ntoa(addr.sin_addr) << ":" << ntohs(addr.sin_port) << endl;
		if (_callbackAccept)
			_callbackAccept(client);
	}
}

void TCPTransportListener::OnRDHUP()
{
}
