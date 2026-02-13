#include "Timer.h"
#include <iostream>
#include <thread>

using namespace std;

inline int64_t GetTimestampMilliseconds()
{
	auto now = std::chrono::system_clock::now();
	auto duration = now.time_since_epoch();
	return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

int64_t starttime = 0;

void oncedosome(int x)
{
	// dosome...
	std::cout << (GetTimestampMilliseconds() - starttime) << " oncedosome...[" << x << "]\n";
}

void dosome(int x)
{
	// dosome...
	std::cout << (GetTimestampMilliseconds() - starttime) << " dosome...[" << x << "]\n";
}

void testTimer()
{
	starttime = GetTimestampMilliseconds();
	std::cout << GetTimestampMilliseconds() - starttime << " testTimer test start...\n";

	auto handle_once = TimerTask::CreateOnce("testtask", 5000,
		[num = 5000]()->void{
		oncedosome(num);
	});
	handle_once->Run();

	auto handle_repeat = TimerTask::CreateRepeat("testtask", 1500, std::bind(dosome, 1500), 1500);
	handle_repeat->Run();

	//no clean!
	TimerTask::CreateRepeat("testtask", 500, std::bind(dosome, 500), 500)->Run();


	std::this_thread::sleep_for(10s);

	handle_once->Clean();
	handle_repeat->Clean();

	std::cout << "testTimer test end...\n";
}

int main(int argc, char* argv[])
{
	system("chcp 65001 > nul"); // 切换到 UTF-8

	testTimer();

	std::cout << "按回车退出程序..." << std::endl;
	std::cin.get();
}