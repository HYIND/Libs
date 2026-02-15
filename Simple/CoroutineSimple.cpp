#include "Coroutine.h"
#include <iostream>
#include <random>
#include <chrono>
#include <string.h>

using namespace std;

Task<bool> timertask(bool shouwake)
{
	CoTimer timer(std::chrono::milliseconds((int64_t)3000));
	if (shouwake)
		timer.wake();
	CoTimer::WakeType result = co_await timer;
	if (result == CoTimer::WakeType::Error)
	{
		std::cout << "CoTimer error!\n";
		co_return false;
	}
	else if (result == CoTimer::WakeType::TIMEOUT)
	{
		std::cout << "CoTimer timeout!\n";
		co_return true;
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

		std::cout << "CoroConnection test start...\n";

		std::string IP = "192.168.58.130";
		int port = 28092;

		BaseSocket socket = co_await CoConnection(IP, port);
		if (socket <= 0)
			std::cout << "CoroConnection connect failed!\n";
		else
			std::cout << "CoroConnection connect seccess!\n";
		std::cout << "CoroConnection test end...\n";
	}
}

void testCoroutine()
{
	auto corotask = testCoroutineTask();
	std::this_thread::sleep_for(1s);
	bool is_done = corotask.is_done();
	std::cout << "corotask done = [" << is_done << "]\n";
	corotask.sync_wait();
}

int main(int argc, char* argv[])
{
	system("chcp 65001 > nul"); // 切换到 UTF-8

	testCoroutine();

	std::cout << "按回车退出程序..." << std::endl;
	std::cin.get();
}