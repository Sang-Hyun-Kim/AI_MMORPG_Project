#include "CorePch.h"
#include "JobTimer.h"

/*--------------
	JobTimer
---------------*/

void JobTimer::Reserve(uint64 tickAfter, std::weak_ptr<JobQueue> owner, JobRef job)
{
	const uint64 executeTick = ::GetTickCount64() + tickAfter;
	JobData* jobData = new JobData(owner, job);

	std::lock_guard<std::mutex> guard(_lock);
	_items.push(TimerItem{ executeTick, jobData });
}

void JobTimer::Distribute(uint64 now)
{
	// 한번에 1 쓰레드만 통과
	if (_distributing.exchange(true) == true)
		return;

	std::vector<TimerItem> items;

	{
		std::lock_guard<std::mutex> guard(_lock);

		while (_items.empty() == false)
		{
			const TimerItem& timerItem = _items.top();
			if (now < timerItem.executeTick)
				break;

			items.push_back(timerItem);
			_items.pop();
		}
	}

	for (TimerItem& item : items)
	{
		if (JobQueueRef owner = item.jobData->owner.lock())
			owner->Push(item.jobData->job);

		delete item.jobData;		
	}

	// 끝났으면 풀어준다.
	_distributing.store(false);
}

void JobTimer::Clear()
{
	std::lock_guard<std::mutex> guard(_lock);

	while (_items.empty() == false)
	{
		const TimerItem& timerItem = _items.top();
		delete timerItem.jobData;
		_items.pop();
	}
}
