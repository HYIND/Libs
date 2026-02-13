#pragma once

#include <list>
#include "CriticalSectionLock.h"
#include "BiDirectionalMap.h"
#include <functional>
#include <chrono>
#include <algorithm>
#include <iostream>

struct TimerTaskHandle;
struct TimerNode {
	int64_t id;
	int64_t expire_time;

	std::shared_ptr<TimerTaskHandle> handle;

	TimerNode(int64_t id, int64_t expiretime, std::shared_ptr <TimerTaskHandle> h);
};

struct TimerTaskHandle
{
	using Callback = std::function<void()>;

	std::string name_;
	uint64_t delay_ms_;
	uint64_t interval_ms_;

	bool repeat_;
	bool valid_;
	Callback callback_;

	int64_t id;

	const std::string& name() const { return name_; }
	bool is_repeat() const { return repeat_; }
	bool is_valid() const { return valid_; }
	Callback callback() { return callback_; }
	void mark_invalid() { valid_ = false; }
	void mark_valid() { valid_ = true; }
};

struct Level {
	uint32_t tick_ms;
	uint32_t slot_count;
	int64_t range_ms;

	int64_t current_tick;

	std::vector<std::list<TimerNode*>> slots;
	CriticalSectionLock mutex;

	Level(uint32_t tickms, uint32_t slotcount);

	void addTimer(int64_t id, int64_t expiretime, std::shared_ptr<TimerTaskHandle> handle);
	void addTimer(TimerNode* node);

	std::vector<TimerNode*> tick();
	std::vector<TimerNode*> cascade();

	bool isRoundComplete() const;
};

class HierarchicalTimeWheel
{
public:
	HierarchicalTimeWheel();
	void Start();
	void Stop();
	bool Running();

public:
	bool Register_Task(std::shared_ptr<TimerTaskHandle>& task);
	bool Cancel_Task(std::shared_ptr<TimerTaskHandle>& task);
	bool UpdateRepeat_Task(std::shared_ptr<TimerTaskHandle>& handle);

private:
	void Run();
	void Cascade(int level_idx);
	int EventProcess(std::shared_ptr<TimerTaskHandle>& task);

private:
	std::atomic<bool> _isrunning;
	std::atomic<int64_t> next_timer_id;
	std::vector<std::unique_ptr<Level>> _levels;
	SafeBiDirectionalMap<int64_t, std::shared_ptr<TimerTaskHandle>> _timers;
	std::thread _run_thread;
};
