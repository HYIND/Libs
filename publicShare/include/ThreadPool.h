#pragma once

#include <thread>
#include "SafeStl.h"
#include <vector>
#include <condition_variable>
#include <functional>
#include <future>

#ifdef _WIN32
#define EXPORT_FUNC __declspec(dllexport)
#elif __linux__
#define EXPORT_FUNC
#endif

// 线程池
class ThreadPool
{
public:
    template <typename T>
    class SubmitHandle
    {
    private:
        std::shared_ptr<std::packaged_task<T()>> task_ptr;
        uint32_t thread_id;

    public:
        SubmitHandle(std::shared_ptr<std::packaged_task<T()>> task_ptr, uint32_t thread_id)
            : task_ptr(std::move(task_ptr)), thread_id(thread_id) {}

        SubmitHandle(const SubmitHandle &) = delete;
        SubmitHandle &operator=(const SubmitHandle &) = delete;

        SubmitHandle(SubmitHandle &&other) noexcept
            : task_ptr(std::move(other.task_ptr)), thread_id(other.thread_id)
        {
            other.thread_id = 0;
        }

        SubmitHandle &operator=(SubmitHandle &&other) noexcept
        {
            if (this != &other)
            {
                task_ptr = std::move(other.task_ptr);
                thread_id = other.thread_id;
                other.thread_id = 0;
            }
            return *this;
        }

        std::future<T> get_future() const
        {
            if (!task_ptr)
            {
                throw std::future_error(std::future_errc::no_state);
            }
            return task_ptr->get_future();
        }

        T get()
        {
            return get_future().get();
        }

        bool valid() const
        {
            return task_ptr != nullptr;
        }

        uint32_t get_thread_id() const
        {
            return thread_id;
        }

        ~SubmitHandle() = default;
    };

private:
    struct ThreadData
    {
        std::thread thread;
        SafeQueue<std::function<void()>> queue;
        std::mutex mutex;
        std::condition_variable cv;
        bool stop = false;
        int id;
    };
    class ThreadWorker // 内置线程工作类
    {
    private:
        ThreadPool *m_pool;                 // 所属线程池
        std::shared_ptr<ThreadData> m_data; // 线程信息
    public:
        // 构造函数
        ThreadWorker(ThreadPool *pool, std::shared_ptr<ThreadData> data) : m_pool(pool), m_data(data) {}
        // 重载()操作
        void operator()();
    };

public:
    EXPORT_FUNC ThreadPool(uint32_t threads_num = 4);

    EXPORT_FUNC ThreadPool(const ThreadPool &) = delete;
    EXPORT_FUNC ThreadPool(ThreadPool &&) = delete;
    EXPORT_FUNC ThreadPool &operator=(const ThreadPool &) = delete;
    EXPORT_FUNC ThreadPool &operator=(ThreadPool &&) = delete;

    EXPORT_FUNC void start();
    EXPORT_FUNC void stop();
    EXPORT_FUNC uint32_t workersize();

private:
    uint32_t GetAvailablieThread();

public:
    template <typename F, typename... Args>
    EXPORT_FUNC auto submit_to(uint32_t thread_id, F &&f, Args &&...args) // 指定线程id
        -> std::shared_ptr<ThreadPool::SubmitHandle<decltype(f(args...))>>
    {
        if (thread_id >= _threads.size())
        {
            throw std::out_of_range("Invalid thread id");
        }

        using ReturnType = decltype(f(args...));

        // 创建一个function
        std::function<decltype(f(args...))()> func = std::bind(std::forward<F>(f), std::forward<Args>(args)...); // 连接函数和参数定义，特殊函数类型，避免左右值错误
        // 封装为packaged_task以便异步操作
        auto task_ptr = std::make_shared<std::packaged_task<ReturnType()>>(func);

        // Warp packaged task into void function
        std::function<void()> warpper_func =
            [task_ptr]()
        {
            (*task_ptr)();
        };

        auto &data = _threads[thread_id];
        data->queue.enqueue(warpper_func);                                                  // 压入安全队列
        data->cv.notify_one();                                                              // 唤醒一个等待中的线程
        return std::make_shared<ThreadPool::SubmitHandle<ReturnType>>(task_ptr, thread_id); // 返回Handle
    }

    template <typename F, typename C, typename... Args>
    EXPORT_FUNC auto submit_to(uint32_t thread_id, F &&f, C &&c, Args &&...args) // 指定线程id
        -> std::shared_ptr<ThreadPool::SubmitHandle<decltype(f(args...))>>
    {

        auto func = submit_to(
            [c = std::forward<C>(c), f = std::forward<F>(f), ... args = std::forward<Args>(args)]() mutable
                -> decltype((c->*f)(args...))
            {
                return (c->*f)(args...);
            });

        return submit_to(thread_id, func);
    }

    template <typename F, typename... Args>
    EXPORT_FUNC auto submit(F &&f, Args &&...args) // 随机分配线程id
        -> std::shared_ptr<ThreadPool::SubmitHandle<decltype(f(args...))>>
    {
        uint32_t thread_id = GetAvailablieThread();
        return submit_to(thread_id, std::forward<F>(f), std::forward<Args>(args)...);
    }

    template <typename F, typename C, typename... Args>
    EXPORT_FUNC auto submit(F &&f, C &&c, Args &&...args) // 随机分配线程id
        -> std::shared_ptr<ThreadPool::SubmitHandle<decltype(f(args...))>>
    {
        uint32_t thread_id = GetAvailablieThread();
        return submit_to(thread_id, std::forward<F>(f), std::forward<C>(c), std::forward<Args>(args)...);
    }

private:
    bool _stop = false;
    uint32_t _threadscount;
    std::atomic<int> next_thread;
    std::vector<std::shared_ptr<ThreadData>> _threads;
};