#include "Connection/TCPTransportListener.h"
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
