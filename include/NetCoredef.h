#pragma once

#include "NetWarpper.h"

extern Buffer HeartBuffer;

struct MsgHeader
{
    int seq = 0;
    int ack = -1;
    int length = 0;
};

struct NetCore_EpollData
{
    int fd;
    Net *Con;
};

class NetCoreProcess
{

public:
    static NetCoreProcess *Instance();
    int Run();
    bool Running();

public:
    bool AddNetFd(Net *Con);
    bool DelNetFd(Net *Con);
    bool SendRes(NetClient *fd);

private:
    void Loop();
    void HeartBeatLoop();
    int EventProcess(epoll_event &event);
    void ThreadEnd();

private:
    bool _isrunning = false;
    int _epoll = epoll_create(300);
    epoll_event _events[500];
    SafeMap<Net *, int> _HeartBeatCount; //<fd->count>
    SafeMap<Net *, NetCore_EpollData *> _EpollData;
};

bool IsHeartBeat(const Buffer &buf);

#define NetCore NetCoreProcess::Instance()
