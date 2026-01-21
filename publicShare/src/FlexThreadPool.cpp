#include "FlexThreadPool.h"
#include <chrono>
#include <assert.h>

static int64_t GetTimeStampSecond()
{
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    auto second = std::chrono::duration_cast<std::chrono::seconds>(duration).count();
    return second;
}

FlexThreadPool::ThreadData::ThreadTask::ThreadTask(std::function<void()> func)
    : func(func)
{
    taskliveflag = std::make_shared<int>(0);
}

FlexThreadPool::FlexThreadPool(uint32_t threads_maxnum, uint32_t worker_expire_second, bool worksteal)
    : _stop{true}, _threadscount(threads_maxnum), _workerExpireTime(worker_expire_second), _worksteal(worksteal)
{
    if (_threadscount <= 0)
        _threadscount = std::thread::hardware_concurrency();
    if (_threadscount < 0)
        _threadscount = 1;

    _threads.reserve(_threadscount);

    for (int i = 0; i < _threadscount; ++i)
    {
        std::shared_ptr<ThreadData> data = std::make_shared<ThreadData>();
        data->id = i;
        _threads.emplace_back(data);
    }
}

FlexThreadPool::~FlexThreadPool()
{
    stop();
}

void FlexThreadPool::start()
{
    bool expected = true;
    if (_stop.compare_exchange_strong(expected, false))
    {
        for (uint32_t i = 0; i < _threads.size(); ++i)
        {
            auto &data = _threads[i];
            if (data->queue.empty() || !data->_stop.load())
                continue;

            StartThread(data);
        }

        _monitorthread = std::make_shared<std::thread>(&FlexThreadPool::MonitorThreadFunc, this);
    }
}

void FlexThreadPool::stop()
{
    bool expected = false;
    if (_stop.compare_exchange_strong(expected, true))
    {
        assert(_monitorthread != nullptr);
        {
            LockGuard guard(_monitor_mutex);
            _monitor_cv.NotifyAll();
        }

        for (auto &data : _threads)
        {
            LockGuard guard(data->queue_mutex);
            data->_stop.store(true);
            data->queue_cv.NotifyAll();
        }
        for (auto &data : _threads)
        {
            if (!data->thread)
                continue;

            if (std::this_thread::get_id() == data->thread->get_id())
                continue;

            if (data->thread->joinable())
                data->thread->join();
            data->thread = nullptr;
            data->queue.clear();
        }
        if (_monitorthread->joinable())
            _monitorthread->join();
    }
}

void FlexThreadPool::stopnow()
{
    bool expected = false;
    if (_stop.compare_exchange_strong(expected, true))
    {
        assert(_monitorthread != nullptr);
        {
            LockGuard guard(_monitor_mutex);
            _monitor_cv.NotifyAll();
        }

        for (auto &data : _threads)
        {
            LockGuard guard(data->queue_mutex);
            data->_stop.store(true);
            data->queue.clear();
            data->queue_cv.NotifyAll();
        }
        for (auto &data : _threads)
        {
            if (!data->thread)
                continue;

            if (std::this_thread::get_id() == data->thread->get_id())
                continue;

            if (data->thread->joinable())
                data->thread->join();
            data->thread = nullptr;
        }
        if (_monitorthread->joinable())
            _monitorthread->join();
    }
}

bool FlexThreadPool::running()
{
    return !(_stop.load());
}

uint32_t FlexThreadPool::workermaxsize()
{
    return _threadscount;
}

void FlexThreadPool::StartThread(std::shared_ptr<ThreadData> data)
{
    if (!data)
        return;
    if (_stop.load())
        return;
    if (!(data->_stop.load()))
        return;

    bool excepted = true;
    if (data->_stop.compare_exchange_strong(excepted, false))
    {
        data->last_active_time = GetTimeStampSecond();
        data->thread = std::make_shared<std::thread>(ThreadWorker(this, data));
    }
}

std::weak_ptr<int> FlexThreadPool::CommitToAvailablieThread(std::function<void()> func)
{
    using ThreadTask = FlexThreadPool::ThreadData::ThreadTask;

    int best_idle_index = -1;
    int min_queue_size = INT32_MAX;

    auto task = std::make_unique<ThreadTask>(func);
    auto taskliveflag = task->taskliveflag;

    // 尝试找到最低负载的线程,负载是0则直接投递给该线程
    for (uint32_t i = 0; i < _threads.size(); ++i)
    {
        auto &data = _threads[i];

        int queuesize = data->queue.size();
        if (queuesize == 0)
        {
            LockGuard guard(data->queue_mutex);
            queuesize = data->queue.size();
            if (queuesize == 0) // 负载是0，直接投递
            {
                data->queue.enqueue(std::move(task));
                data->last_active_time = GetTimeStampSecond();
                if (data->_stop.load() && !_stop.load())
                    StartThread(data);
                data->queue_cv.NotifyOne();
                return std::weak_ptr<int>(taskliveflag);
            }
        }
        if (queuesize < min_queue_size)
        {
            min_queue_size = queuesize;
            best_idle_index = i;
        }
    }

    assert(best_idle_index >= 0);

    // 选取负载最小的线程投递任务
    auto &data = _threads[best_idle_index];
    LockGuard guard(data->queue_mutex);
    data->queue.enqueue(std::move(task));
    data->last_active_time = GetTimeStampSecond();
    if (data->_stop.load() && !_stop.load())
        StartThread(data);
    data->queue_cv.NotifyOne();
    return std::weak_ptr<int>(taskliveflag);
}

void FlexThreadPool::MonitorThreadFunc()
{
    while (!_stop.load())
    {
        // 每5秒检查一次空闲线程
        LockGuard lock(_monitor_mutex);
        _monitor_cv.WaitFor(lock, std::chrono::seconds(3));
        if (_stop.load())
            break;

        std::vector<std::shared_ptr<std::thread>> waitjointhreads;
        uint64_t curtime = GetTimeStampSecond();

        // 检查所有线程的空闲状态
        for (uint32_t i = 0; i < _threads.size(); ++i)
        {
            auto &data = _threads[i];
            if (data->_stop.load())
                continue;

            if (curtime - data->last_active_time > _workerExpireTime &&
                data->queue.empty() &&
                data->_is_idle)
            {
                LockGuard guard(data->queue_mutex);
                if (!data->queue.empty() || !data->_is_idle)
                    continue;
                data->_stop.store(true);
                data->queue_cv.NotifyOne();
                waitjointhreads.emplace_back(std::move(data->thread));
                data->thread = nullptr;
            }
        }

        if (!waitjointhreads.empty())
        {
            for (auto &thread : waitjointhreads)
            {
                if (!thread)
                    continue;
                if (thread->joinable())
                    thread->join();
            }
        }
    }
}

FlexThreadPool::ThreadWorker::ThreadWorker(FlexThreadPool *pool, std::shared_ptr<ThreadData> data)
    : m_pool(pool), m_data(data), last_steal_threadid(-1)
{
}

void FlexThreadPool::ThreadWorker::operator()()
{
    using ThreadTask = FlexThreadPool::ThreadData::ThreadTask;

    auto needstop = [this]() -> bool
    {
        return (m_pool->_stop.load() || m_data->_stop.load()) && m_data->queue.empty();
    };

    assert(m_data);
    m_data->last_active_time = GetTimeStampSecond();
    bool dequeued = false;
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

        std::unique_ptr<ThreadTask> task;
        if (m_pool->_worksteal && m_data->queue.empty() && stealTask(task) && task)
        {
            {
                LockGuard guard(m_data->queue_mutex);
                m_data->_is_idle = false;
                m_data->last_active_time = GetTimeStampSecond();
            }
            assert(task->func);
            task->func();
            m_data->_is_idle = true;
        }
        else
        {
            {
                {
                    LockGuard guard(m_data->queue_mutex);
                    if (needstop()) // 获取锁后检查一次
                    {
                        m_data->_stop.store(true);
                        return;
                    }
                    if (m_data->queue.empty())
                    {
                        m_data->queue_cv.WaitFor(guard, std::chrono::milliseconds(20));
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
                        m_data->last_active_time = GetTimeStampSecond();
                    }
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
}

bool FlexThreadPool::ThreadWorker::stealTask(std::unique_ptr<ThreadTask> &task)
{
    if (m_pool->_threadscount <= 1 && m_pool->_stop.load(std::memory_order_acquire) || m_data->_stop.load(std::memory_order_acquire))
        return false;

    static auto steal = [](
                            std::shared_ptr<FlexThreadPool::ThreadData> &data,
                            std::unique_ptr<ThreadTask> &task) -> bool
    {
        if (data->_stop.load() || data->queue.empty())
            return false;

        LockGuard guard(data->queue_mutex, true);
        if (!guard.isownlock())
            return false;
        if (data->_stop.load() || data->queue.empty())
            return false;

        if (data->queue.dequeue(task) && task)
            return true;

        return false;
    };

    for (auto &data : m_pool->_threads)
    {
        if (data == m_data)
            continue;
        if (last_steal_threadid == data->id)
            continue;
        if (steal(data, task))
        {
            last_steal_threadid = data->id;
            // std::cout << "id [" << m_data->id << "] stealTask from [" << last_steal_threadid << "]" << '\n';
            return true;
        }
    }

    if (last_steal_threadid >= 0)
    {
        assert(m_pool->_threads[last_steal_threadid]);
        auto &data = m_pool->_threads[last_steal_threadid];
        if (steal(data, task))
        {
            // std::cout << "id [" << m_data->id << "] stealTask from [" << last_steal_threadid << "]" << '\n';
            return true;
        }
    }

    return false;
}
