#include "Coroutine.h"

#ifdef __linux__
#include "CoroutineScheduler_Linux.h"
#else 
#include "CoroutineScheduler_Win.h"
#endif

#include "SpinLock.h"

using ContextData = std::unordered_map<std::string, std::shared_ptr<void>>;
class PUBLICSHARE_API CoroutineContextImpl
{

public:
	static bool isCoroutineThread();
	static void setCurrent(CoroHandle coro);
	static CoroHandle getCurrent();
	static void clearCurrent();
	static void setParent(CoroHandle child, CoroHandle parent);
	static void setParent(CoroHandle child, std::thread::id parent);
	static CoroHandle getCurrentRoot();
	static CoroHandle getRoot(CoroHandle coro);
	static void clearParent(CoroHandle child);
	static bool isAncestor(CoroHandle descendant, CoroHandle ancestor);
	static bool isAncestor(CoroHandle descendant, std::thread::id threadid);
	static void clearSyncWaitChild(CoroHandle child);
	static void printAncestorLink(CoroHandle coro);
	static void printCurrentAncestorLink();

	static bool setData(const std::string& key, std::shared_ptr<void> ptr)
	{
		CoroHandle current = getCurrent();
		if (!current)
			return false;

		s_contexts[current][key] = ptr;
		return true;
	}

	static void* getData(const std::string& key, bool skipcurrent = false) {
		CoroHandle current = getCurrent();
		if (!current)
			return nullptr;

		if (skipcurrent)
		{
			if (!s_coroInfos.Find(current, current) || !current)
				return nullptr;
		}

		do
		{
			if (s_contexts.Exist(current) && s_contexts[current].find(key) != s_contexts[current].end())
				return s_contexts[current][key].get();
		} while (s_coroInfos.Find(current, current) && current);

		return nullptr;
	}
private:
	static SafeUnorderedMap<CoroHandle, CoroHandle> s_coroInfos;	// 全局协程关系表 子协程->父协程
	static SafeUnorderedMap<CoroHandle, std::thread::id> s_SyncWait_Infos;// 被sync_wait协程->普通线程
	static SafeUnorderedMap<CoroHandle, ContextData> s_contexts;  // 协程上下文
};

thread_local std::coroutine_handle<> s_currentCoro = nullptr;
SafeUnorderedMap<CoroHandle, CoroHandle> CoroutineContextImpl::s_coroInfos = SafeUnorderedMap<CoroHandle, CoroHandle>();
SafeUnorderedMap<CoroHandle, std::thread::id> CoroutineContextImpl::s_SyncWait_Infos = SafeUnorderedMap<CoroHandle, std::thread::id>();
SafeUnorderedMap<CoroHandle, ContextData> CoroutineContextImpl::s_contexts = SafeUnorderedMap<CoroHandle, ContextData>();

bool CoroutineContextImpl::isCoroutineThread() {
	return (bool)s_currentCoro;
}

void CoroutineContextImpl::setCurrent(CoroHandle coro) {
	s_currentCoro = coro;
}

CoroHandle CoroutineContextImpl::getCurrent() {
	return s_currentCoro;
}

void CoroutineContextImpl::clearCurrent() {
	s_currentCoro = nullptr;
}

void CoroutineContextImpl::setParent(CoroHandle child, CoroHandle parent) {
	s_coroInfos.EnsureInsert(child, parent);
}

void CoroutineContextImpl::setParent(CoroHandle child, std::thread::id threadid) {
	s_SyncWait_Infos.EnsureInsert(child, threadid);
}

CoroHandle CoroutineContextImpl::getCurrentRoot() {
	return getRoot(getCurrent());
}

CoroHandle CoroutineContextImpl::getRoot(CoroHandle coro) {
	CoroHandle current = coro;
	while (s_coroInfos.Find(current, current) && current) {}
	return current;
}

void CoroutineContextImpl::clearParent(CoroHandle child) {
	s_SyncWait_Infos.Erase(child);
	s_coroInfos.Erase(child);
	s_contexts.Erase(child);
}

bool CoroutineContextImpl::isAncestor(CoroHandle descendant, CoroHandle ancestor)
{
	if (ancestor == descendant) return true;

	CoroHandle current = descendant;
	while (s_coroInfos.Find(current, current) && current)
	{
		if (current == ancestor)
			return true;
	}
	return false;
}

bool CoroutineContextImpl::isAncestor(CoroHandle descendant, std::thread::id threadid)
{
	CoroHandle root = getRoot(descendant);

	std::thread::id id;
	if (!s_SyncWait_Infos.Find(root, id))
		return false;

	return id == threadid;
}

void CoroutineContextImpl::printAncestorLink(CoroHandle coro)
{
	CoroHandle current = coro;
	auto gurad = s_coroInfos.MakeLockGuard();
	std::cout << "printAncestorLink: ";
	std::cout << current.address();
	while (s_coroInfos.Find(current, current) && current)
		std::cout << "->" << current.address();

	std::thread::id threadid;
	if (s_SyncWait_Infos.Find(current, threadid))
		std::cout << "->" << threadid << "(threadid)\n";
	else
		std::cout << "->" << "null\n";
}

void CoroutineContextImpl::printCurrentAncestorLink()
{
	printAncestorLink(getCurrent());
}

bool CoroutineContext::isCoroutineThread() { return getImpl().isCoroutineThread(); }
void CoroutineContext::setCurrent(CoroHandle coro) { getImpl().setCurrent(coro); }
CoroHandle CoroutineContext::getCurrent() { return getImpl().getCurrent(); }
void CoroutineContext::clearCurrent() { getImpl().clearCurrent(); }
void CoroutineContext::setParent(CoroHandle child, CoroHandle parent) { getImpl().setParent(child, parent); }
void CoroutineContext::setParent(CoroHandle child, std::thread::id parent) { getImpl().setParent(child, parent); }
CoroHandle CoroutineContext::getCurrentRoot() { return getImpl().getCurrentRoot(); }
CoroHandle CoroutineContext::getRoot(CoroHandle coro) { return getImpl().getRoot(coro); }
void CoroutineContext::clearParent(CoroHandle child) { getImpl().clearParent(child); }
bool CoroutineContext::isAncestor(CoroHandle descendant, CoroHandle ancestor) { return getImpl().isAncestor(descendant, ancestor); }
bool CoroutineContext::isAncestor(CoroHandle descendant, std::thread::id threadid) { return getImpl().isAncestor(descendant, threadid); }
bool CoroutineContext::setDataImpl(const std::string& key, std::shared_ptr<void> ptr) { return getImpl().setData(key, ptr); }
void* CoroutineContext::getDataImpl(const std::string& key, bool skipcurrent) { return getImpl().getData(key, skipcurrent); }
CoroutineContextImpl& CoroutineContext::getImpl() {
	static CoroutineContextImpl impl;
	return impl;
}


bool yieldAwaiter::await_ready()
{
	return false;
}

void yieldAwaiter::await_suspend(std::coroutine_handle<> coro)
{
	CoroutineScheduler::Instance()->RegisterTaskCoroutine(coro);
}

void yieldAwaiter::await_resume()
{
}

yieldAwaiter yield::operator co_await()
{
	return {};
}

TaskHandle::TaskHandle(std::coroutine_handle<> coroutine)
	: coroutine(coroutine)
{
}
TaskHandle::~TaskHandle()
{
}

//最小化协程
class DelayDelete_Task {
public:
	struct promise_type {
		DelayDelete_Task get_return_object() {
			return DelayDelete_Task{
				std::coroutine_handle<promise_type>::from_promise(*this)
			};
		}

		std::suspend_never initial_suspend() noexcept { return {}; }	// 启动不挂起	
		std::suspend_never final_suspend() noexcept { return {}; }// 最终不挂起

		void return_void() {}
		void unhandled_exception() {}
	};

	explicit DelayDelete_Task(std::coroutine_handle<promise_type> h) : handle_(h) {}
	~DelayDelete_Task() {
		if (handle_) {
			handle_.destroy();
		}
	}

	DelayDelete_Task(DelayDelete_Task&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}

	DelayDelete_Task& operator=(DelayDelete_Task&& other) noexcept {
		if (this != &other) {
			if (handle_) {
				handle_.destroy();
			}
			handle_ = std::exchange(other.handle_, {});
		}
		return *this;
	}

	DelayDelete_Task(const DelayDelete_Task&) = delete;
	DelayDelete_Task& operator=(const DelayDelete_Task&) = delete;

	std::coroutine_handle<> get_handle() const {
		return handle_;
	}

private:
	std::coroutine_handle<promise_type> handle_;
};

constexpr size_t Delete_Worker_Count = 4;
class Delaydelete_Scheduler
{
	struct Worker
	{
		SpinLock _resume_spin;
		std::shared_ptr<std::coroutine_handle<>> _res;
		DelayDelete_Task _scheduler_handle;

		Worker(size_t index) :_scheduler_handle(Delaydelete_Scheduler::scheduler_co(index)) {}
	};
public:
	static std::vector<Worker*> workers;
	static std::atomic<bool> isInitStart;
	static std::atomic<bool> isInitDone;
	static size_t _current_worker;

public:
	static DelayDelete_Task scheduler_co(size_t index)
	{
		while (true)
		{
			co_await std::suspend_always{};
			Delaydelete_Scheduler::workers[index]->_res.reset();
			Delaydelete_Scheduler::workers[index]->_resume_spin.unlock();
		}
	}

	static void Initialize()
	{
		for (int i = 0; i < Delete_Worker_Count; i++)
		{
			auto worker = new Worker(i);
			workers.push_back(worker);
		}
	}

	static std::coroutine_handle<> RequestDelete(std::shared_ptr<std::coroutine_handle<>> p)
	{
		bool expected = false;
		if (isInitStart.compare_exchange_strong(expected, true))
		{
			Initialize();
			isInitDone.store(true);
		}

		while (!isInitDone.load()) {}

		size_t idx = _current_worker++ % Delete_Worker_Count;

		workers[idx]->_resume_spin.lock();
		workers[idx]->_res = std::move(p);
		return workers[idx]->_scheduler_handle.get_handle();
	}
};

std::vector<Delaydelete_Scheduler::Worker*> Delaydelete_Scheduler::workers = {};
std::atomic<bool> Delaydelete_Scheduler::isInitStart{ false };
std::atomic<bool> Delaydelete_Scheduler::isInitDone{ false };
size_t Delaydelete_Scheduler::_current_worker = 0;

std::coroutine_handle<> DeleteLater(std::shared_ptr<std::coroutine_handle<>> p)
{
	return Delaydelete_Scheduler::RequestDelete(std::move(p));
}

CoroCriticalSectionLock::CoroCriticalSectionLock() {}
CoroCriticalSectionLock::~CoroCriticalSectionLock() {}

bool CoroCriticalSectionLock::try_lock()
{
	bool isCorotinueThread = CoroutineContext::isCoroutineThread();
	std::thread::id thread_id = std::this_thread::get_id();
	CoroHandle coro_id = CoroutineContext::getCurrent();

	LockGuard guard(mutex);
	if (_status.type == HolderStatus::Type::None)
	{
		_status.type = isCorotinueThread
			? HolderStatus::Type::Coroutine
			: HolderStatus::Type::Thread;

		if (_status.type == HolderStatus::Type::Coroutine)
			_status.coro_id = coro_id;
		else if (_status.type == HolderStatus::Type::Thread)
			_status.thread_id = thread_id;

		_status.recursive_count = 1;
		return true;
	}
	else if (_status.type == HolderStatus::Type::Coroutine)
	{
		if (!isCorotinueThread || !CoroutineContext::isAncestor(coro_id, _status.coro_id))
			return false;

		_status.recursive_count++;
		return true;
	}
	else if (_status.type == HolderStatus::Type::Thread)
	{
		if (!isCorotinueThread)
		{
			if (thread_id != _status.thread_id)
				return false;

			_status.recursive_count++;
			return true;
		}
		else
		{
			if (!CoroutineContext::isAncestor(coro_id, _status.thread_id))
				return false;

			_status.recursive_count++;
			return true;
		}
	}
}

void CoroCriticalSectionLock::lock()
{
	int count = 1000;	//快速检查1000次
	do
	{
		std::this_thread::yield();
		if (try_lock())
			return;
		count--;
	} while (count > 0);


	while (true)
	{
		LockGuard guard(mutex);
		cv.WaitFor(guard, std::chrono::milliseconds(200));
		if (try_lock())
			return;
	}
}

void CoroCriticalSectionLock::unlock()
{
	LockGuard guard(mutex);
	if (_status.type == HolderStatus::Type::None)
		return;

	if (_status.type == HolderStatus::Type::Coroutine)
	{
		if (!CoroutineContext::isCoroutineThread() || !CoroutineContext::isAncestor(CoroutineContext::getCurrent(), _status.coro_id))
			throw std::runtime_error("No own lock when unlock!");
	}
	else if (_status.type == HolderStatus::Type::Thread)
	{
		if (!CoroutineContext::isCoroutineThread())
		{
			if (std::this_thread::get_id() != _status.thread_id)
				throw std::runtime_error("No own lock when unlock!");
		}
		else
		{
			if (!CoroutineContext::isAncestor(CoroutineContext::getCurrent(), _status.thread_id))
				throw std::runtime_error("No own lock when unlock!");
		}
	}


	if (--_status.recursive_count <= 0)
	{
		// 完全释放锁
		_status.type = HolderStatus::Type::None;
		_status.coro_id = nullptr;
		_status.thread_id = std::thread::id();
		cv.NotifyOne();
	}
}
