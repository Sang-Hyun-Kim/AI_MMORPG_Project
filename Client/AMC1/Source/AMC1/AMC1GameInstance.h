#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "TimerManager.h"
#include "AMC1GameInstance.generated.h"

class FSocket;
class FNetworkWorker;

UCLASS()
class AMC1_API UAMC1GameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	virtual void Init() override;
	virtual void Shutdown() override;

	void ConnectToServer();
	void DisconnectFromServer();
	
	void SendPacket(TSharedPtr<class SendBuffer> SendBuf);
	void SendPing();

private:
	FSocket* Socket;
	TSharedPtr<FNetworkWorker> NetworkWorker;
	FTimerHandle PingTimerHandle;
};
