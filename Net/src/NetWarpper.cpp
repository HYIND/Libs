#include "NetWarpper.h"
#include "NetCoredef.h"

using namespace std;

int NewServerSocket(const std::string &IP, uint16_t socket_port, __socket_type protocol, sockaddr_in &sock_addr)
{
    bzero(&sock_addr, sizeof(sock_addr));
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
    bzero(&sock_addr, sizeof(sock_addr));
    sock_addr.sin_family = AF_INET;
    sock_addr.sin_port = htons(socket_port);

    sock_addr.sin_addr.s_addr = inet_addr(IP.c_str());

    int socket_fd = socket(PF_INET, protocol, 0);

    return socket_fd;
}

Net::Net(SocketType type, bool isclient) : _type(type), _isclient(isclient) {}
int Net::GetFd() { return this->_fd; }
SocketType Net::GetType() { return this->_type; }
sockaddr_in Net::GetAddr() { return _addr; }
char *Net::GetIPAddr() { return inet_ntoa(_addr.sin_addr); }
uint16_t Net::GetPort() { return ntohs(_addr.sin_port); }
NetType Net::GetNetType() { return _isclient ? NetType::Client : NetType::Listener; }

NetListener::NetListener(SocketType type) : Net(type, false) {}
NetListener::~NetListener()
{
    if (_fd <= 0)
        return;

    ReleaseListener();
}

bool NetListener::Listen(const string &IP, int Port)
{
    if (this->_fd > 0)
        ReleaseListener();

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

    NetCore->AddNetFd(this);

    return true;
}

bool NetListener::ReleaseListener()
{
    if (close(_fd) == -1)
        return false;

    NetCore->DelNetFd(this);
    _fd = -1;
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

void NetListener::OnRDHUP() {}

NetClient::NetClient(SocketType type) : Net(type, true) {}
NetClient::~NetClient()
{
    if (_fd <= 0)
        return;

    Release();
}

bool NetClient::Connet(const std::string &IP, uint16_t Port)
{
    if (_fd > 0)
        Release();

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

    NetCore->AddNetFd(this);
    return true;
}
void NetClient::Apply(const int fd, const sockaddr_in &sockaddr, const SocketType type)
{
    if (_fd > 0)
        Release();

    this->_fd = fd;
    this->_addr = sockaddr;
    this->_type = type;
    NetCore->AddNetFd(this);
}

bool NetClient::Release()
{
    bool result = false;
    NetCore->DelNetFd(this);
    if (close(_fd) == -1)
        result = false;

    _fd = -1;
    result = true;

    for (auto it : _RecvDatas)
        SAFE_DELETE(it.second);
    _RecvDatas.clear();

    return result;
}

bool NetClient::AsyncSend(const Buffer &buffer, int ack)
{
    try
    {
        if (!buffer.Data() || buffer.Length() < 0)
            return true;
        MsgHeader header;
        header.seq = this->seq;
        this->seq++;
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
        return true;
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
        unique_lock<mutex> awaitlck(task->_mtx);

        MsgHeader header;
        header.seq = this->seq;
        this->seq++;
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

int NetClient::Recv(Buffer &buffer, int length)
{
    Buffer buf(length);
    int result = ::recv(_fd, buf.Data(), length, MSG_NOSIGNAL);
    if (result > 0)
        buffer.QuoteFromBuf(buf);
    return result;
}

void NetClient::BindMessageCallBack(function<void(NetClient *, Package *pak, Buffer *AckResponse)> callback)
{
    this->_callbackMessage = callback;
}
void NetClient::BindRDHUPCallBack(function<void(NetClient *)> callback)
{
    this->_callbackRDHUP = callback;
}
std::map<int, Package *> &NetClient::GetRecvData() { return _RecvDatas; }
SafeQueue<Package *> &NetClient::GetSendData() { return _SendDatas; }
std::mutex &NetClient::GetSendMtx() { return _SendResMtx; }

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
            auto it = _RecvDatas.erase(_RecvDatas.begin());
            delete it->second;
        }
        _RecvDatas[header.seq] = pak;

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

void NetClient::OnRDHUP()
{
    cout << "OnRDHUP" << endl;
    // if (fd == this->_fd)
    //     NetCore->DelNetFd(this);

    if (_callbackRDHUP)
        _callbackRDHUP(this);
}