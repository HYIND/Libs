#include "ThreadPool.h"

ThreadPool::ThreadPool(uint32_t threads_num)
    : _stop(true), next_thread(0)
{
    if (threads_num <= 0)
        threads_num = std::thread::hardware_concurrency();
    _threads.reserve(threads_num);
    _threadscount = threads_num;
}
void ThreadPool::start()
{
    if (!_stop)
        return;

    _stop = false;
    if (_threads.empty())
    {
        for (int i = 0; i < _threadscount; ++i)
        {
            std::shared_ptr<ThreadData> data = std::make_shared<ThreadData>();
            data->id = i;
            data->stop = false;
            data->thread = std::thread(ThreadWorker(this, data)); // 分配工作线程
            _threads.emplace_back(data);
        }
    }
    else
    {
        for (int i = 0; i < _threads.size(); ++i)
        {
            std::lock_guard<std::mutex> lock(_threads[i]->mutex);
            _threads[i]->stop = false;
            _threads[i]->thread = std::thread(ThreadWorker(this, _threads[i]));
        }
    }
}
void ThreadPool::stop()
{
    _stop = true;

    for (int i = 0; i < _threads.size(); ++i) // 唤醒所有工作线程
    {
        std::lock_guard<std::mutex> lock(_threads[i]->mutex);
        _threads[i]->stop = true;
        _threads[i]->cv.notify_all();
    }
    for (int i = 0; i < _threads.size(); ++i)
    {
        if (_threads[i]->thread.joinable())
            _threads[i]->thread.join();
        _threads[i]->queue.clear();
    }
}

uint32_t ThreadPool::workersize()
{
    return _threadscount;
}

uint32_t ThreadPool::GetAvailablieThread()
{
    uint32_t thread_id = next_thread.fetch_add(1, std::memory_order_relaxed) % _threads.size();
    return thread_id;
}

void ThreadPool::ThreadWorker::operator()()
{
    bool dequeued; // 是否取出队列中元素
    while (true)
    {
        std::function<void()> func; // 定义基础函数类func
        {
            std::unique_lock<std::mutex> lock(m_data->mutex);
            if (!m_pool->_stop && !m_data->stop && m_data->queue.empty())
            {
                m_data->cv.wait(lock);
            }
            if ((m_pool->_stop || m_data->stop) && m_data->queue.empty())
            {
                return;
            }

            // 取出任务队列中的元素
            dequeued = m_data->queue.dequeue(func);
        }

        // 如果成功取出，执行工作函数
        if (dequeued)
            func();
    }
}