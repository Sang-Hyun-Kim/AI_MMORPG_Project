#include "NetworkWorker.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "HAL/RunnableThread.h"

FNetworkWorker::FNetworkWorker(FSocket* InSocket)
	: Socket(InSocket), bRunning(false), Thread(nullptr)
{
	RecvBuffer.SetNumUninitialized(4096); // 4KB 버퍼 초기화
	Thread = FRunnableThread::Create(this, TEXT("NetworkWorkerThread"));
}

FNetworkWorker::~FNetworkWorker()
{
	Destroy();
}

bool FNetworkWorker::Init()
{
	bRunning = true;
	UE_LOG(LogTemp, Log, TEXT("[FNetworkWorker] Thread Initialized"));
	return true;
}

uint32 FNetworkWorker::Run()
{
	while (bRunning)
	{
		if (Socket == nullptr || Socket->GetConnectionState() != SCS_Connected)
		{
			break;
		}

		uint32 Size = 0;
		if (Socket->HasPendingData(Size))
		{
			int32 ReadBytes = 0;
			bool bRecv = Socket->Recv(RecvBuffer.GetData(), RecvBuffer.Num(), ReadBytes);
			if (bRecv && ReadBytes > 0)
			{
				UE_LOG(LogTemp, Log, TEXT("[FNetworkWorker] Received %d bytes from Server."), ReadBytes);
			}
		}
		else
		{
			// 너무 잦은 루핑을 방지하기 위한 대기 (1ms)
			FPlatformProcess::Sleep(0.001f);
		}
	}

	return 0;
}

void FNetworkWorker::Stop()
{
	bRunning = false;
	UE_LOG(LogTemp, Log, TEXT("[FNetworkWorker] Thread Stop Requested"));
}

void FNetworkWorker::Exit()
{
	UE_LOG(LogTemp, Log, TEXT("[FNetworkWorker] Thread Exited"));
}

void FNetworkWorker::Destroy()
{
	Stop();

	if (Thread != nullptr)
	{
		Thread->WaitForCompletion();
		delete Thread;
		Thread = nullptr;
	}
}
