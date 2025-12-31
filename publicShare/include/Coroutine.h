#pragma once

#include <memory>
#include <coroutine>
#include <chrono>
#include <exception>
#include <optional>
#include <type_traits>
#include <utility>
#include "CriticalSectionLock.h"
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <atomic>

void UnBindCoroutineThreadFromScheduler(std::coroutine_handle<> coroutine);

struct RegisterTaskAwaiter
{
    bool await_ready();
    void await_suspend(std::coroutine_handle<> coro);
    void await_resume();
};

// 前置声明
template <typename T>
class Task;

template <typename T>
struct TaskPromiseBase
{
    std::exception_ptr exception_;
    std::coroutine_handle<> continuation_;

    std::shared_ptr<CriticalSectionLock> _continuationlock = std::make_shared<CriticalSectionLock>();

    auto initial_suspend() noexcept
    {
        return RegisterTaskAwaiter{};
    }

    auto final_suspend() noexcept
    {
        struct FinalAwaiter
        {
            TaskPromiseBase *promise;
            bool await_ready() noexcept { return false; }
            std::coroutine_handle<> await_suspend(std::coroutine_handle<> completed_coro) noexcept
            {
                LockGuard lock(*(promise->_continuationlock));
                if (promise->continuation_)
                    return promise->continuation_;
                return std::noop_coroutine();
            }

            void await_resume() noexcept {}
        };
        return FinalAwaiter{this};
    }

    void unhandled_exception() noexcept
    {
        exception_ = std::current_exception();
    }

    void set_continuation(std::coroutine_handle<> continuation) noexcept
    {
        continuation_ = continuation;
    }
};

// 非 void 类型的 Promise
template <typename T>
struct TaskPromise : TaskPromiseBase<T>
{
    std::optional<T> value_;

    Task<T> get_return_object() noexcept;

    void return_value(T &&value)
    {
        value_.emplace(std::move(value));
    }

    void return_value(const T &value)
    {
        value_.emplace(value);
    }

    T &result()
    {
        if (this->exception_)
        {
            std::rethrow_exception(this->exception_);
        }
        return value_.value();
    }

    ~TaskPromise() = default;
};

// void 特化的 Promise
template <>
struct TaskPromise<void> : TaskPromiseBase<void>
{
    Task<void> get_return_object() noexcept;

    void return_void() noexcept {}

    void result()
    {
        if (exception_)
        {
            std::rethrow_exception(exception_);
        }
    }

    ~TaskPromise() = default;
};

template <typename T>
struct TaskAwaiter
{
    std::coroutine_handle<TaskPromise<T>> coroutine_;
    std::shared_ptr<CriticalSectionLock> _continuationlock;
    TaskAwaiter(std::coroutine_handle<TaskPromise<T>> coroutine, std::shared_ptr<CriticalSectionLock> lock) noexcept
        : coroutine_(coroutine), _continuationlock(lock) {}

    bool await_ready() noexcept
    {
        return coroutine_ && coroutine_.done();
    }

    bool await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept
    {
        if (!coroutine_ || coroutine_.done())
        {
            return false; // 无效协程，不挂起
        }

        LockGuard lock(*_continuationlock);
        if (coroutine_.done())
            return false;

        auto &promise = coroutine_.promise();
        promise.set_continuation(awaiting_coroutine);

        return true;
    }

    // 非 void 版本的 await_resume
    template <typename U = T>
    std::enable_if_t<!std::is_void_v<U>, U> await_resume()
    {
        if (!coroutine_ || coroutine_.done())
        {
            if (coroutine_)
            {
                return coroutine_.promise().result();
            }
            throw std::runtime_error("Invalid coroutine");
        }
        throw std::runtime_error("Coroutine not completed");
    }

    // void 特化的 await_resume
    template <typename U = T>
    std::enable_if_t<std::is_void_v<U>, void> await_resume()
    {
        if (!coroutine_ || coroutine_.done())
        {
            if (coroutine_)
            {
                coroutine_.promise().result();
                return;
            }
            throw std::runtime_error("Invalid coroutine");
        }
        throw std::runtime_error("Coroutine not completed");
    }
};

// Task 类定义
template <typename T>
class Task
{
public:
    using promise_type = TaskPromise<T>;
    using value_type = T;

    // 从协程句柄构造
    explicit Task(std::coroutine_handle<promise_type> coroutine, std::shared_ptr<CriticalSectionLock> lock) noexcept
        : coroutine_(coroutine), _continuationlock(lock) {}

    // 移动构造
    Task(Task &&other) noexcept
        : coroutine_(std::exchange(other.coroutine_, nullptr)), _continuationlock(other._continuationlock) {}

    // 移动赋值
    Task &operator=(Task &&other) noexcept
    {
        if (this != &other)
        {
            Release();
            coroutine_ = std::exchange(other.coroutine_, nullptr);
            _continuationlock = other._continuationlock;
        }
        return *this;
    }

    // 禁止拷贝
    Task(const Task &) = delete;
    Task &operator=(const Task &) = delete;

    ~Task()
    {
        Release();
    }

    void Release()
    {
        if (coroutine_)
        {
            UnBindCoroutineThreadFromScheduler(coroutine_);
            coroutine_.destroy();
            coroutine_ = nullptr;
        }
    }

    // 判断是否有效
    explicit operator bool() const noexcept
    {
        return static_cast<bool>(coroutine_);
    }

    // 支持 co_await
    TaskAwaiter<T> operator co_await() const noexcept
    {
        return TaskAwaiter<T>{coroutine_, _continuationlock};
    }

    // 同步等待结果
    template <typename U = T>
    std::enable_if_t<!std::is_void_v<U>, U> sync_wait()
    {
        if (coroutine_)
        {
            while (!coroutine_.done())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            return coroutine_.promise().result();
        }
        throw std::runtime_error("Invalid task");
    }

    template <typename U = T>
    std::enable_if_t<std::is_void_v<U>, void> sync_wait()
    {
        if (coroutine_)
        {
            while (!coroutine_.done())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            coroutine_.promise().result();
            return;
        }
        throw std::runtime_error("Invalid task");
    }

    // 恢复协程执行
    void resume()
    {
        if (coroutine_ && !coroutine_.done())
        {
            coroutine_.resume();
        }
    }

    // 检查是否完成
    bool is_done() const noexcept
    {
        return !coroutine_ || coroutine_.done();
    }

    // 获取内部协程句柄
    auto get_handle() const noexcept
    {
        return coroutine_;
    }

private:
    std::coroutine_handle<promise_type> coroutine_;
    std::shared_ptr<CriticalSectionLock> _continuationlock;

    // 友元声明
    template <typename U>
    friend struct TaskPromise;
};

// 定义 get_return_object
template <typename T>
inline Task<T> TaskPromise<T>::get_return_object() noexcept
{
    return Task<T>{
        std::coroutine_handle<TaskPromise<T>>::from_promise(*this), TaskPromiseBase<T>::_continuationlock};
}

inline Task<void> TaskPromise<void>::get_return_object() noexcept
{
    return Task<void>{
        std::coroutine_handle<TaskPromise<void>>::from_promise(*this), TaskPromiseBase<void>::_continuationlock};
}

// 协程定时器包装器
class CoTimer
{
public:
    enum class WakeType
    {
        Error = -1,
        RUNNING = 0,
        TIMEOUT,
        MANUAL_WAKE
    };

    struct Handle
    {
        Handle();
        ~Handle();

        int timer_fd;
        bool active;

        std::coroutine_handle<> coroutine;
        std::atomic<bool> corodone{false};
        CriticalSectionLock corolock;

        std::atomic<WakeType> wakeresult;
    };

    struct Awaiter
    {
        Awaiter(std::shared_ptr<CoTimer::Handle> handle);

        bool await_ready() const noexcept;
        void await_suspend(std::coroutine_handle<> h);
        WakeType await_resume() noexcept;

        std::shared_ptr<CoTimer::Handle> handle;
        uint64_t expected_count;
        std::coroutine_handle<> coroutine;
    };

public:
    CoTimer(std::chrono::milliseconds timeout, bool periodic = false);
    ~CoTimer();
    Awaiter operator co_await(); // 协程等待操作
    void wake();                 // 立即唤醒

private:
    std::shared_ptr<Handle> handle;
};

// 协程连接器包装器
class CoConnection
{
public:
    struct Awaiter
    {
        Awaiter(int fd, sockaddr_in addr);

        bool await_ready();
        void await_suspend(std::coroutine_handle<> coro);
        int await_resume();

        int fd;
        sockaddr_in addr;
        int res;
    };

public:
    CoConnection(int fd, sockaddr_in addr);
    ~CoConnection();
    Awaiter operator co_await();

private:
    int fd;
    sockaddr_in addr;
};
