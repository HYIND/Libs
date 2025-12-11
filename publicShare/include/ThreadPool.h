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
private:
    bool _stop = false;
    uint32_t _threadscount;
    SafeQueue<std::function<void()>> _queue;
    std::vector<std::thread> _threads;
    std::mutex _mutex;
    std::condition_variable _cv;

    class ThreadWorker // 内置线程工作类
    {
    private:
        int m_id;           // 工作id
        ThreadPool *m_pool; // 所属线程池
    public:
        // 构造函数
        ThreadWorker(ThreadPool *pool, const int id) : m_pool(pool), m_id(id) {}
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

    template <typename F, typename... Args>
    EXPORT_FUNC auto submit(F &&f, Args &&...args) -> std::future<decltype(f(args...))>
    {
        // 创建一个function
        std::function<decltype(f(args...))()> func = std::bind(std::forward<F>(f), std::forward<Args>(args)...); // 连接函数和参数定义，特殊函数类型，避免左右值错误
        // 封装为packaged_task以便异步操作
        auto task_ptr = std::make_shared<std::packaged_task<decltype(f(args...))()>>(func);

        // Warp packaged task into void function
        std::function<void()> warpper_func =
            [task_ptr]()
        {
            (*task_ptr)();
        };

        _queue.enqueue(warpper_func);  // 压入安全队列
        _cv.notify_one();              // 唤醒一个等待中的线程
        return task_ptr->get_future(); // 返回先前注册future
    }

    template <typename F, typename C, typename... Args>
    EXPORT_FUNC auto submit(F &&f, C &&c, Args &&...args) -> std::future<decltype((c->*f)(args...))>
    {

        return submit(
            [c = std::forward<C>(c), f = std::forward<F>(f), ... args = std::forward<Args>(args)]() mutable
                -> decltype((c->*f)(args...))
            {
                return (c->*f)(args...);
            });
    }
};