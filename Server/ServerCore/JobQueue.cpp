#include "CorePch.h"
#include "JobQueue.h"
#include "JobTimer.h"

void JobQueue::ReserveJob(uint64 tickAfter, JobRef job)
{
	GJobTimer->Reserve(tickAfter, shared_from_this(), job);
}

void JobQueue::Push(JobRef job, bool pushOnly)
{
	const int32 prevCount = _jobCount.fetch_add(1);
	
	// 락프리 큐에 일감을 밀어넣습니다 (Wait-Free)
	_jobs.Push(job);

	// 이전에 실행중인 일감이 없었고(prevCount == 0), 지금 당장 실행해야 한다면(pushOnly == false)
	if (prevCount == 0 && pushOnly == false)
	{
		// 이 스레드가 JobQueue의 실행 권한을 획득하여 Execute 진입
		Execute();
	}
}

void JobQueue::Execute()
{
	while (true)
	{
		std::vector<JobRef> jobs;
		
		// 락프리 큐에서 Pop을 시도합니다. Pop은 단일 스레드(현재 이 스레드)에서만 
		// 호출되므로 안전합니다.
		JobRef job;
		while (_jobs.Pop(job))
		{
			jobs.push_back(std::move(job));
		}

		// 꺼낸 일감들을 처리합니다.
		for (JobRef& j : jobs)
		{
			// [TD-02 B2] 잡 1개의 예외는 그 잡만 버립니다. 아래 fetch_sub 에 반드시 도달해야 룸이 영구 정지하지 않습니다.
			try
			{
				j->Execute();
			}
			catch (const std::exception& e)
			{
				MLOG_ERROR(Sys) << "[JobQueue] job threw: " << e.what();
			}
			catch (...)
			{
				MLOG_ERROR(Sys) << "[JobQueue] job threw: unknown exception";
			}
		}

		// 방금 처리한 개수만큼 실행 카운트를 뺍니다.
		const int32 remaining = _jobCount.fetch_sub(static_cast<int32>(jobs.size()));

		// 만약 처리하는 도중에 누군가 새로 Push를 해서 남은 일감이 있다면 루프를 계속 돕니다.
		// 남은 일감이 없으면 실행 권한을 반납하고 루프를 탈출합니다.
		if (remaining == static_cast<int32>(jobs.size()))
		{
			break;
		}
	}
}
