#include "ThreadPool.h"

ThreadPool::ThreadPool(int threads_num)
    : _threads(std::vector<std::thread>(threads_num)), _stop(false)
{
    if (threads_num <= 0)
        threads_num = std::thread::hardware_concurrency();
    _threads.reserve(threads_num);
}
void ThreadPool::start()
{
    for (int i = 0; i < _threads.size(); ++i)
    {
        _threads.at(i) = std::thread(ThreadWorker(this, i)); // 分配工作线程
    }
}
void ThreadPool::stop()
{
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _stop = true;
    }

    _cv.notify_all(); // 通知，唤醒所有工作线程
    for (int i = 0; i < _threads.size(); ++i)
    {
        if (_threads.at(i).joinable()) // 判断线程是否在等待
        {
            _threads.at(i).join(); // 将线程加入到等待队列
        }
    }
}

void ThreadPool::ThreadWorker::operator()()
{
    std::function<void()> func; // 定义基础函数类func
    bool dequeued;              // 是否取出队列中元素
    while (true)
    {
        {
            // 为线程环境加锁，互访问工作线程的休眠和唤醒
            std::unique_lock<std::mutex> lock(m_pool->_mutex);

            // 如果任务队列为空，阻塞当前线程
            if (!m_pool->_stop && m_pool->_queue.empty())
            {
                m_pool->_cv.wait(lock); // 等待条件变量通知，开启线程
            }

            // 如果线程池已停止且队列为空，退出线程
            if (m_pool->_stop && m_pool->_queue.empty())
            {
                return;
            }

            // 取出任务队列中的元素
            dequeued = m_pool->_queue.dequeue(func);
        }

        // 如果成功取出，执行工作函数
        if (dequeued)
            func();
    }
}