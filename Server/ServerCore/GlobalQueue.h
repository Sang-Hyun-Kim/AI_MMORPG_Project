#pragma once
#include "CorePch.h"
#include "LockFreeQueue.h"
#include "JobQueue.h"

/*----------------
	GlobalQueue
-----------------*/

class GlobalQueue
{
public:
	GlobalQueue() = default;
	~GlobalQueue() = default;

	void Push(JobQueueRef jobQueue);
	JobQueueRef Pop();

private:
	LockFreeQueue<JobQueueRef> _jobQueues;
};
