#pragma once

#include "CorePch.h"
#include "Job.h"
#include "LockFreeQueue.h"

/*--------------
	JobQueue
---------------*/

// C++20: enable_shared_from_this를 사용하여 자기 자신의 참조 카운트를 유지합니다.
class JobQueue : public std::enable_shared_from_this<JobQueue>
{
public:
	void DoAsync(CallbackType&& callback)
	{
		Push(std::make_shared<Job>(std::move(callback)));
	}

	template<typename T, typename Ret, typename... Args>
	void DoAsync(Ret(T::*memFunc)(Args...), Args... args)
	{
		std::shared_ptr<T> owner = std::static_pointer_cast<T>(shared_from_this());
		Push(std::make_shared<Job>(owner, memFunc, std::forward<Args>(args)...));
	}

	// 지연 실행 (예약 후 즉시 스레드 반납, GJobTimer가 만료 시 Push)
	void DoTimer(uint64 tickAfter, CallbackType&& callback)
	{
		JobRef job = std::make_shared<Job>(std::move(callback));
		ReserveJob(tickAfter, job);
	}

	template<typename T, typename Ret, typename... Args>
	void DoTimer(uint64 tickAfter, Ret(T::*memFunc)(Args...), Args... args)
	{
		std::shared_ptr<T> owner = std::static_pointer_cast<T>(shared_from_this());
		JobRef job = std::make_shared<Job>(owner, memFunc, std::forward<Args>(args)...);
		ReserveJob(tickAfter, job);
	}

public:
	void					Push(JobRef job, bool pushOnly = false);
	void					Execute();

private:
	// GJobTimer->Reserve를 호출하는 헬퍼. 순환 참조 방지를 위해 구현은 .cpp에 배치.
	void					ReserveJob(uint64 tickAfter, JobRef job);

protected:
	// 뮤텍스 기반 큐 대신 완벽한 Lock-Free 큐를 사용합니다.
	LockFreeQueue<JobRef>	_jobs;
	
	// 현재 JobQueue가 실행 중인지 나타내는 플래그 (0: 대기, 1: 실행중)
	std::atomic<int32>		_jobCount = 0;
};

using JobQueueRef = std::shared_ptr<JobQueue>;

