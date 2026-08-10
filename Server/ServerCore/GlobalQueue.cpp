#include "CorePch.h"
#include "GlobalQueue.h"

void GlobalQueue::Push(JobQueueRef jobQueue)
{
	_jobQueues.Push(jobQueue);
}

JobQueueRef GlobalQueue::Pop()
{
	JobQueueRef jobQueue = nullptr;
	_jobQueues.Pop(jobQueue);
	return jobQueue;
}
