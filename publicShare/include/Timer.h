#pragma once

#include <functional>
#include <memory>
#include <string>

class TimerTask : public std::enable_shared_from_this<TimerTask>
{
public:
    using Callback = std::function<void()>;

    static std::shared_ptr<TimerTask> CreateOnce(
        const std::string &name,
        uint64_t delay_ms,
        Callback callback);

    static std::shared_ptr<TimerTask> CreateRepeat(
        const std::string &name,
        uint64_t interval_ms,
        Callback callback,
        uint64_t delay_ms = 0);

    bool Run();
    bool Stop();

    ~TimerTask();
    void Clean(); // 关闭定时器并清理资源

public:
    const std::string &name() const { return name_; }
    uint64_t interval() const { return interval_ms_; }
    bool is_repeat() const { return repeat_; }
    bool is_valid() const { return valid_; }
    int timer_fd() const { return timer_fd_; }
    Callback callback() { return callback_; };

    void mark_invalid() { valid_ = false; }

private:
    TimerTask(const std::string &name,
              uint64_t interval_ms,
              bool repeat,
              Callback callback,
              uint64_t delay_ms = 0);

    TimerTask(const TimerTask &) = delete;
    TimerTask &operator=(const TimerTask &) = delete;

    bool create_timer_fd();

private:
    std::string name_;
    uint64_t interval_ms_;
    bool repeat_;
    bool valid_;
    Callback callback_;
    int timer_fd_;
    uint64_t delay_ms_;
};
