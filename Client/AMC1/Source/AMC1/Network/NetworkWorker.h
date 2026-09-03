#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"

class FSocket;
class UAMC1GameInstance;

class AMC1_API FNetworkWorker : public FRunnable
{
public:
	FNetworkWorker(FSocket* InSocket, UAMC1GameInstance* InGameInstance);
	virtual ~FNetworkWorker();

	// FRunnable interface
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;
	virtual void Exit() override;

	void Destroy();

private:
	FSocket* Socket;
	bool bRunning;
	FRunnableThread* Thread;

	// 임시 수신 버퍼 (Protobuf 파싱 제외)
	TArray<uint8> RecvBuffer;

	// GameInstance 약참조 (GC 파괴 시 자동 무효화)
	TWeakObjectPtr<UAMC1GameInstance> WeakGameInstance;
};
