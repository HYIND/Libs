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

Task<bool> testconnection(std::string IP, int port)
{
	BaseSocket socket = co_await CoConnection(IP, port);
	co_return socket <= 0;
}

Task<int> testadd(int a, int b)
{
	co_await yield();
	co_return a + b;
}

Task<void> testCoroutineTask()
{
	{ // CoTimer
		std::cout << "CoTimer test start...\n";
		auto task_shoudown = timertask(true);
		bool result_shoudown = co_await task_shoudown;
		auto task = timertask(false);
		bool result = co_await task;
		std::cout << "CoTimer test end...\n";
	}

	{ // CoSleep
		std::cout << "CoSleep test start...\n";
		co_await CoSleep(1s);
		std::cout << "CoSleep test end...\n";
	}

	{// CoTask
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

		if (co_await testconnection(IP, port))
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

Task<void> root();
Task<void> child1();
Task<void> grandchild();
Task<void> child2();

Task<void> testroot() {
	// 根协程设置上下文
	CoroutineContext::setData("request_id", std::string("req-123"));
	CoroutineContext::setData("user_id", 456);

	auto reqId = CoroutineContext::getData<std::string>("request_id");
	auto userId = CoroutineContext::getData<int>("user_id");
	std::cout << "root-----\n";
	if (reqId)
		cout << "reqId = " << *reqId << '\n';
	if (userId)
		cout << "userId = " << *userId << '\n';

	co_await child1();  // 子协程自动继承
	co_await child2();  // 子协程自动继承
	co_return;
}

Task<void> child1() {

	//仅本协程极子协程可见
	bool result = CoroutineContext::setData("request_id", std::string("child1-req-234"));

	//显式回溯父协程
	if (auto parentReqId = CoroutineContext::getData<std::string>("request_id", true))
		*parentReqId = std::string("child1-modify-req-234");

	auto reqId = CoroutineContext::getData<std::string>("request_id");
	auto userId = CoroutineContext::getData<int>("user_id");

	std::cout << "child1-----\n";
	if (reqId)
		cout << "reqId = " << *reqId << '\n';
	if (userId)
		cout << "userId = " << *userId << '\n';

	co_await grandchild();  // 孙子协程继续继承
	co_return;
}

Task<void> child2() {
	// 添加新的上下文
	CoroutineContext::setData("child2_only", true);

	std::cout << "child2-----\n";
	auto child2_only = CoroutineContext::getData<bool>("request_id");
	auto reqId = CoroutineContext::getData<std::string>("request_id");
	auto userId = CoroutineContext::getData<int>("user_id");
	if (child2_only)
		cout << "child2_only = " << (*child2_only ? string("true") : string("false")) << '\n';
	if (reqId)
		cout << "reqId = " << *reqId << '\n';
	if (userId)
		cout << "userId = " << *userId << '\n';

	co_return;
}

Task<void> grandchild() {

	std::cout << "grandchild-----\n";
	auto child2_only = CoroutineContext::getData<bool>("child2_only");
	auto reqId = CoroutineContext::getData<std::string>("request_id");
	auto userId = CoroutineContext::getData<int>("user_id");
	if (child2_only)
		cout << "child2_only = " << *child2_only << '\n';
	if (reqId)
		cout << "reqId = " << *reqId << '\n';
	if (userId)
		cout << "userId = " << *userId << '\n';
	co_return;
}

int main(int argc, char* argv[])
{
#ifdef _WIN32
	system("chcp 65001 > nul"); // 切换到 UTF-8
#endif

	//测试协程上下文系统
	//testroot().sync_wait();

	testCoroutine();
	std::cout << "按回车退出程序..." << std::endl;
	std::cin.get();
}
