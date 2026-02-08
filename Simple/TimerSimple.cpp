#include "Timer.h"
#include <unistd.h>
#include <iostream>

using namespace std;

void oncedosome(int x)
{
    // dosome...
    std::cout << "oncedosome...[" << x << "]\n";
}

void dosome(int x)
{
    // dosome...
    std::cout << "dosome...[" << x << "]\n";
}

void testTimer()
{
    std::cout << "testTimer test start...\n";

    auto handle_once = TimerTask::CreateOnce("testtask", 3000, std::bind(oncedosome, 5));
    handle_once->Run();

    auto handle_repeat = TimerTask::CreateRepeat("testtask", 1000, std::bind(dosome, 10), 100);
    handle_repeat->Run();

    sleep(5);
    handle_once->Stop();
    handle_repeat->Stop();
    sleep(5);
    handle_once->Run();
    handle_repeat->Run();
    sleep(5);
    handle_once->Stop();
    handle_repeat->Stop();

    std::cout << "testTimer test end...\n";
}

int main(int argc, char *argv[])
{
    testTimer();

    std::cout << "按回车退出程序..." << std::endl;
    std::cin.get();
}