#include "ThreadPool.h"
#include "FlexThreadPool.h"
#include <iostream>

using namespace std;

int dosome(int x)
{
	// dosome...
	std::this_thread::sleep_for(50ms);
	return x;
}

void testThreadPool()
{
	std::cout << "testThreadPool test start...\n";

	ThreadPool pool(4);
	pool.start();
	std::vector<std::shared_ptr<ThreadPool::SubmitHandle<int>>> handles;
	for (int i = 0; i < 300; i++)
	{
		std::shared_ptr<ThreadPool::SubmitHandle<int>> handle;
		int temp = i % 3;
		if (temp == 0)
			handle = pool.submit(dosome, i);
		else if (temp == 1)
			handle = pool.submit_to(1, dosome, i);
		else if (temp == 2)
			handle = pool.submit_to(3, dosome, i);

		if (handle)
			handles.emplace_back(handle);
	}
	for (auto& handle : handles)
	{
		int result = handle->get();
		std::cout << "testThreadPool task result [" << result << "]\n";
	}
	pool.stop();

	std::cout << "testThreadPool test end...\n";
}

void testFlexThreadPool()
{
	std::cout << "testFlexThreadPool test start...\n";

	FlexThreadPool pool(8);
	pool.start();
	std::vector<std::shared_ptr<FlexThreadPool::SubmitHandle<int>>> handles;
	for (int i = 0; i < 300; i++)
	{
		auto handle = pool.submit(dosome, i);
		handles.emplace_back(handle);
	}
	for (auto& handle : handles)
	{
		int result = handle->get();
		std::cout << "testFlexThreadPool task result [" << result << "]\n";
	}
	pool.stop();

	std::cout << "testFlexThreadPool test end...\n";
}

int main(int argc, char* argv[])
{
	testThreadPool();
	testFlexThreadPool();

	std::cout << "按回车退出程序..." << std::endl;
	std::cin.get();
}