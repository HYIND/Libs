#include "ThreadPool.h"
#include <assert.h>

ThreadPool::ThreadData::ThreadTask::ThreadTask(std::function<void()> func)
    : func(func)
{
    taskliveflag = std::make_shared<int>(0);
}

ThreadPool::ThreadPool(uint32_t threads_num)
    : _stop{true}, _threadscount(threads_num), next_thread(0)
{
    if (_threadscount <= 0)
        _threadscount = std::thread::hardware_concurrency();
    if (_threadscount < 0)
        _threadscount = 1;
    _threads.reserve(threads_num);

    for (int i = 0; i < _threadscount; ++i)
    {
        std::shared_ptr<ThreadData> data = std::make_shared<ThreadData>();
        data->id = i;
        data->_stop.store(true);
        _threads.emplace_back(data);
    }
}

ThreadPool::~ThreadPool()
{
    stop();
}

void ThreadPool::start()
{
    bool expected = true;
    if (_stop.compare_exchange_strong(expected, false))
    {
        for (int i = 0; i < _threads.size(); ++i)
        {
            auto &data = _threads[i];
            data->_stop.store(false);
            data->thread = std::thread(ThreadWorker(this, data));
        }
    }
}

void ThreadPool::stop()
{
    bool expected = false;
    if (_stop.compare_exchange_strong(expected, true))
    {
        for (auto &data : _threads)
        {
            LockGuard guard(data->queue_mutex);
            data->_stop.store(true);
            data->queue_cv.NotifyAll();
        }
        for (auto &data : _threads)
        {
            if (data->thread.joinable())
                data->thread.join();
            data->queue.clear();
        }
    }
}

void ThreadPool::stopnow()
{
    bool expected = false;
    if (_stop.compare_exchange_strong(expected, true))
    {
        for (auto &data : _threads)
        {
            LockGuard guard(data->queue_mutex);
            data->_stop.store(true);
            data->queue.clear();
            data->queue_cv.NotifyAll();
        }
        for (auto &data : _threads)
        {
            if (data->thread.joinable())
                data->thread.join();
        }
    }
}

bool ThreadPool::running()
{
    return !(_stop.load());
}

uint32_t ThreadPool::workersize()
{
    return _threadscount;
}

std::weak_ptr<int> ThreadPool::CommitTask(uint32_t thread_id, std::function<void()> func)
{
    using ThreadTask = ThreadPool::ThreadData::ThreadTask;

    auto task = std::make_unique<ThreadTask>(func);
    auto taskliveflag = task->taskliveflag;

    if (thread_id >= _threads.size())
    {
        throw std::out_of_range("Invalid thread id");
    }

    auto &data = _threads[thread_id];
    {
        LockGuard lock(data->queue_mutex);
        data->queue.enqueue(std::move(task));
        data->queue_cv.NotifyOne();
    }

    return std::weak_ptr<int>(taskliveflag);
}

uint32_t ThreadPool::GetAvailablieThread()
{
    int best_idle_index = -1;
    int min_task_size = INT32_MAX;

    // 尝试找到最低负载的线程,负载是0则直接选中
    for (uint32_t i = 0; i < _threads.size(); ++i)
    {
        auto &data = _threads[i];

        int tasksize = data->queue.size() + (data->_is_idle ? 0 : 1);
        if (tasksize == 0)
        {
            LockGuard guard(data->queue_mutex);
            tasksize = data->queue.size() + (data->_is_idle ? 0 : 1);
            if (tasksize == 0) // 负载是0，直接选中该线程
                return i;
        }
        if (tasksize < min_task_size)
        {
            min_task_size = tasksize;
            best_idle_index = i;
        }
    }

    // 选取负载最小的线程
    assert(best_idle_index >= 0);

    return best_idle_index;
}

ThreadPool::ThreadWorker::ThreadWorker(ThreadPool *pool, std::shared_ptr<ThreadData> data)
    : m_pool(pool), m_data(data)
{
}

void ThreadPool::ThreadWorker::operator()()
{
    using ThreadTask = ThreadPool::ThreadData::ThreadTask;

    auto needstop = [this]() -> bool
    {
        return (m_pool->_stop.load() || m_data->_stop.load()) && m_data->queue.empty();
    };

    bool dequeued;
    while (true)
    {
        {
            LockGuard guard(m_data->queue_mutex);
            if (needstop())
            {
                m_data->_stop.store(true);
                return;
            }
        }

        dequeued = false;
        std::unique_ptr<ThreadTask> task;
        {
            LockGuard guard(m_data->queue_mutex);
            if (needstop()) // 获取锁后检查一次
            {
                m_data->_stop.store(true);
                return;
            }
            if (m_data->queue.empty())
            {
                m_data->queue_cv.Wait(guard);
            }
            if (needstop()) // 重新获取锁后检查一次
            {
                m_data->_stop.store(true);
                return;
            }
            if (!m_data->queue.empty())
            {
                dequeued = m_data->queue.dequeue(task);
                m_data->_is_idle = false;
            }
        }

        if (dequeued && task)
        {
            assert(task->func);
            task->func();
        }
        m_data->_is_idle = true;
    }
}