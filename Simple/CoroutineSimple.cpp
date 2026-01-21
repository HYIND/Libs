#include "Session/CustomTcpSession.h"
#include "Session/CustomWebSocketSession.h"
#include "Core/NetCore.h"
#include "fmt/core.h"
#include <iostream>
#include <random>
#include <chrono>

using namespace std;

Task<bool> timertask(bool shouwake)
{
    CoTimer timer(std::chrono::milliseconds((int64_t)3000));
    if (shouwake)
        timer.wake();
    CoTimer::WakeType result = co_await timer;
    if (result == CoTimer::WakeType::Error)
    {
        co_return false;
        std::cout << "CoTimer error!\n";
    }
    else if (result == CoTimer::WakeType::TIMEOUT)
    {
        co_return true;
        std::cout << "CoTimer timeout!\n";
    }
    else
    {
        std::cout << "CoTimer manual_wake!\n";
        co_return true;
    }
}

Task<int> testadd(int a, int b)
{
    co_return a + b;
}

Task<void> testCoroutineTask()
{
    { // CoTimer
        std::cout << "CoTimer test start...\n";
        auto task_shoudown = timertask(true);
        auto task = timertask(false);
        bool result_shoudown = co_await task_shoudown;
        bool result = co_await task;
        std::cout << "CoTimer test end...\n";
    }

    { // CoSleep
        std::cout << "CoSleep test start...\n";
        co_await CoSleep(1s);
        std::cout << "CoSleep test end...\n";
    }

    {
        std::cout << "CoroTaskAdd test start...\n";
        std::cout << "CoroTaskAdd input 2+3\n";
        int addresult = co_await testadd(2, 3);
        std::cout << "CoroTaskAdd addresult " << addresult << "\n";
        std::cout << "CoroTaskAdd test end...\n";
    }

    {
        auto NewClientSocket = [](const std::string &IP, uint16_t socket_port, __socket_type protocol, sockaddr_in &sock_addr) -> int
        {
            memset(&sock_addr, '0', sizeof(sock_addr));
            sock_addr.sin_family = AF_INET;
            sock_addr.sin_port = htons(socket_port);

            sock_addr.sin_addr.s_addr = inet_addr(IP.c_str());

            int socket_fd = socket(PF_INET, protocol, 0);

            return socket_fd;
        };

        std::cout << "CoroConnection test start...\n";

        std::string IP = "192.168.58.130";
        int port = 5555;

        sockaddr_in addr;
        int fd = NewClientSocket(IP, port, SOCK_STREAM, addr);
        int result = co_await CoConnection(fd, addr);
        if (fd == -1)
            std::cout << "CoroConnection connect failed!\n";
        else
            std::cout << "CoroConnection connect seccess!\n";
        std::cout << "CoroConnection test end...\n";
    }
}

void testCoroutine()
{
    auto corotask = testCoroutineTask();
    sleep(1);
    bool is_done = corotask.is_done();
    std::cout << "corotask done = [" << is_done << "]\n";
    corotask.sync_wait();
}

int main(int argc, char *argv[])
{
    testCoroutine();
}