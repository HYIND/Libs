#include "TimeWheel.h"
#include "HighPrecisionTimer.h"

namespace TimeWheel
{
	inline int64_t GetTimestampMilliseconds()
	{
		auto now = std::chrono::system_clock::now();
		auto duration = now.time_since_epoch();
		return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
	}

	inline int64_t GetTimestampSecond()
	{
		auto now = std::chrono::system_clock::now();
		auto duration = now.time_since_epoch();
		return std::chrono::duration_cast<std::chrono::seconds>(duration).count();
	}
}

TimerNode::TimerNode(int64_t id, int64_t expiretime, std::shared_ptr <TimerTaskHandle> h)
	: id(id), expire_time(expiretime), handle(h) {
}


Level::Level(uint32_t tickms, uint32_t slotcount)
	: tick_ms(tickms), slot_count(slotcount), current_tick(0)
{
	slots.resize(slot_count);
	range_ms = tick_ms * slot_count;
}

void Level::addTimer(int64_t id, int64_t expiretime, std::shared_ptr<TimerTaskHandle> handle)
{
	if (!handle)
		return;

	LockGuard lock(mutex);

	int64_t remaining = expiretime - TimeWheel::GetTimestampMilliseconds();
	int64_t expire_ticks = current_tick + std::max(int64_t(0), (remaining / tick_ms)) + 1;
	uint32_t slot = expire_ticks % slot_count;
	slots[slot].insert(slots[slot].end(), new TimerNode(id, expiretime, handle));
	//std::cout << "addTimer slot = " << slot << '\n';
}

void Level::addTimer(TimerNode* node)
{
	if (!node || !node->handle)
		return;

	LockGuard lock(mutex);

	int64_t remaining = node->expire_time - TimeWheel::GetTimestampMilliseconds();
	int64_t expire_ticks = current_tick + std::max(int64_t(0), (remaining / tick_ms)) + 1;
	uint32_t slot = expire_ticks % slot_count;
	slots[slot].insert(slots[slot].end(), node);
	//std::cout << "addTimer TimerNode slot = " << slot << '\n';
}

// 推进一格，返回需要执行的定时器
std::vector<TimerNode*> Level::tick() {
	LockGuard lock(mutex);

	current_tick++;
	current_tick = current_tick % slot_count;
	int64_t current_slot_idx = current_tick;
	auto& current_slot = slots[current_slot_idx];

	auto current_time = TimeWheel::GetTimestampMilliseconds();
	auto next_range_time = TimeWheel::GetTimestampMilliseconds() + tick_ms;

	std::vector<TimerNode*> bucket;
	for (auto it = current_slot.begin(); it != current_slot.end();)
	{
		TimerNode* node = *it;
		if (!node)
		{
			it = current_slot.erase(it);
			continue;
		}

		if (node->expire_time <= current_time)
		{
			it = current_slot.erase(it);
			bucket.emplace_back(node);
			//std::cout << node->expire_time << "<=" << TimeWheel::GetTimestampMilliseconds() << "emplace_back!\n";
			continue;
		}
		else
		{
			//std::cout << node->expire_time << ">" << TimeWheel::GetTimestampMilliseconds() << "timeout!\n";
		}
		it++;
	}

	std::sort(bucket.begin(), bucket.end(), [](const TimerNode* a, const TimerNode* b)->bool
		{
			return a->expire_time < b->expire_time;
		});

	return bucket;
}

// 推进一格，预取下一格的定时器做降级处理
std::vector<TimerNode*> Level::cascade()
{
	LockGuard lock(mutex);

	current_tick++;
	current_tick = current_tick % slot_count;
	int64_t next_slot_idx = (current_tick + 1) % slot_count;
	auto& next_slot = slots[next_slot_idx];

	auto current_time = TimeWheel::GetTimestampMilliseconds();
	auto next_time = TimeWheel::GetTimestampMilliseconds() + tick_ms;


	std::vector<TimerNode*> bucket;
	for (auto it = next_slot.begin(); it != next_slot.end();)
	{
		TimerNode* node = *it;
		if (!node)
		{
			it = next_slot.erase(it);
			continue;
		}

		if (node->expire_time <= next_time)
		{
			it = next_slot.erase(it);
			bucket.emplace_back(node);
			continue;
		}
		it++;
	}

	std::sort(bucket.begin(), bucket.end(), [](const TimerNode* a, const TimerNode* b)->bool
		{
			return a->expire_time < b->expire_time;
		});

	return bucket;
}

bool Level::isRoundComplete() const {
	return current_tick % slot_count == 0;
}

HierarchicalTimeWheel::HierarchicalTimeWheel()
	: _isrunning(false), next_timer_id{ 0 }
{
	constexpr uint32_t leavl_1_tick_ms = 10;
	constexpr uint32_t leavl_1_slots = 1000;

	constexpr uint32_t leavl_2_tick_ms = leavl_1_tick_ms * leavl_1_slots;
	constexpr uint32_t leavl_2_slots = 18;

	constexpr uint32_t leavl_3_tick_ms = leavl_2_tick_ms * leavl_2_slots;
	constexpr uint32_t leavl_3_slots = 20;

	_levels.emplace_back(std::make_unique<Level>(leavl_1_tick_ms, leavl_1_slots));
	_levels.emplace_back(std::make_unique<Level>(leavl_2_tick_ms, leavl_2_slots));
	_levels.emplace_back(std::make_unique<Level>(leavl_3_tick_ms, leavl_3_slots));
}

void HierarchicalTimeWheel::Start()
{
	try
	{
		_isrunning = true;
		HighPrecisionTimer::Initialize();
		_run_thread = std::thread(&HierarchicalTimeWheel::Run, this);
	}
	catch (const std::exception& e)
	{
		_isrunning = false;
		std::cerr << e.what() << '\n';
	}
}

void HierarchicalTimeWheel::Stop()
{
	_isrunning = false;
	if (_run_thread.joinable()) {
		_run_thread.join();
	}
	HighPrecisionTimer::Uninitialize();
}

bool HierarchicalTimeWheel::Running()
{
	return _isrunning;
}

bool HierarchicalTimeWheel::Register_Task(std::shared_ptr<TimerTaskHandle>& handle)
{
	if (!Running() || !handle)
		return false;

	int64_t timer_id = next_timer_id++;
	int64_t expiretime = TimeWheel::GetTimestampMilliseconds() + handle->delay_ms_;

	handle->id = timer_id;

	LockGuard guard = _timers.MakeLockGuard();

	// 选择合适的层级
	for (size_t i = 0; i < _levels.size(); ++i)
	{
		// 当前层能容纳这个定时器
		if (handle->delay_ms_ < _levels[i]->range_ms || i == _levels.size() - 1)
		{
			// 保存映射关系
			handle->mark_valid();
			_timers.InsertOrUpdate(timer_id, handle);
			_levels[i]->addTimer(timer_id, expiretime, handle);
			return true;
		}
	}

	return false;
}

bool HierarchicalTimeWheel::Cancel_Task(std::shared_ptr<TimerTaskHandle>& handle)
{
	if (!handle)
		return false;

	LockGuard guard = _timers.MakeLockGuard();

	int64_t timer_id;
	if (!_timers.FindByRight(handle, timer_id))
		return false;

	_timers.EraseByRight(handle);
	handle->mark_invalid();

	// std::cout << "[TimerManager] Unregistered timer '" << task->name() << "'" << std::endl;

	return true;
}

bool HierarchicalTimeWheel::Wake_Task(std::shared_ptr<TimerTaskHandle>& handle)
{
	if (!Running() || !handle || !handle->is_valid())
		return false;

	constexpr uint64_t delay_ms = 0;

	int64_t timer_id = next_timer_id++;
	int64_t expiretime = TimeWheel::GetTimestampMilliseconds() + delay_ms;

	handle->id = timer_id;

	LockGuard guard = _timers.MakeLockGuard();
	if (!handle->is_valid())
		return false;

	// 选择合适的层级
	for (size_t i = 0; i < _levels.size(); ++i)
	{
		// 当前层能容纳这个定时器
		if (delay_ms < _levels[i]->range_ms || i == _levels.size() - 1)
		{
			// 保存映射关系
			_timers.InsertOrUpdate(timer_id, handle);
			_levels[i]->addTimer(timer_id, expiretime, handle);
			return true;
		}
	}

	return false;
}

bool HierarchicalTimeWheel::UpdateRepeat_Task(std::shared_ptr<TimerTaskHandle>& handle)
{
	if (!Running() || !handle || !handle->is_valid())
		return false;

	int64_t timer_id = next_timer_id++;
	int64_t expiretime = TimeWheel::GetTimestampMilliseconds() + handle->interval_ms_;

	handle->id = timer_id;

	LockGuard guard = _timers.MakeLockGuard();
	if (!handle->is_valid())
		return false;

	// 选择合适的层级
	for (size_t i = 0; i < _levels.size(); ++i)
	{
		// 当前层能容纳这个定时器
		if (handle->interval_ms_ < _levels[i]->range_ms || i == _levels.size() - 1)
		{
			// 保存映射关系
			_timers.InsertOrUpdate(timer_id, handle);
			_levels[i]->addTimer(timer_id, expiretime, handle);
			return true;
		}
	}

	return false;
}

void HierarchicalTimeWheel::Run()
{
	//std::cout << "TimerProcess , EventLoop\n";

	if (_levels.empty())
	{
		_isrunning = false;
		return;
	}

	const int64_t BASE_TICK_MS = _levels[0]->tick_ms;
	auto next_tick_time = TimeWheel::GetTimestampMilliseconds();

	//static auto start_time = next_tick_time;
	//static auto tickcount = 0;

	bool shouldshutdown = false;
	do
	{
		if (shouldshutdown)
			break;

		auto now = TimeWheel::GetTimestampMilliseconds();
		if (now < next_tick_time) {
			if (HighPrecisionTimer::IsInitialized())
				HighPrecisionTimer::MSleep(1);
			else
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			continue;
		};

		//if (tickcount % 100 == 0)
		//	std::cout << "tickcount =" << tickcount
		//	<< "  curtime = " << now - start_time
		//	<< " 误差 = " << now - start_time - (tickcount * BASE_TICK_MS)
		//	<< '\n';
		//tickcount++;


		// 处理层1
		auto expired_Node = _levels[0]->tick();
		for (auto& node : expired_Node) {
			if (!node || !node->handle)
				continue;
			int64_t id;
			if (!_timers.FindByRight(node->handle, id) || id != node->id)
			{
				delete node;
				continue;
			}

			EventProcess(node->handle);
			delete node;
		}

		// 层1走完一圈，触发层2
		if (_levels[0]->isRoundComplete()) {
			Cascade(1);
		}

		next_tick_time = TimeWheel::GetTimestampMilliseconds() + BASE_TICK_MS;

	} while (_isrunning);
}

void HierarchicalTimeWheel::Cascade(int level_idx) {
	if (level_idx >= _levels.size()) return;

	// 预取当前层
	auto willexpire_Node = _levels[level_idx]->cascade();

	// 降级到下一层
	for (auto& node : willexpire_Node) {
		_levels[level_idx - 1]->addTimer(node);  // 立即执行或降级
	}

	// 如果当前层也走完一圈，继续向上
	if (_levels[level_idx]->isRoundComplete()) {
		Cascade(level_idx + 1);
	}
}

int HierarchicalTimeWheel::EventProcess(std::shared_ptr<TimerTaskHandle>& handle)
{
	if (!handle || !handle->is_valid())
		return -1;

	// 执行回调
	try
	{
		if (handle->callback())
			std::invoke(handle->callback());
	}
	catch (const std::exception& e)
	{
		std::cerr << "[Timer] Callback exception for timername '"
			<< handle->name() << "': " << e.what() << std::endl;
	}

	// 如果是单次定时器，移除
	if (!handle->is_repeat())
	{
		Cancel_Task(handle);
	}
	else
	{
		UpdateRepeat_Task(handle);
	}
	return 1;
}
