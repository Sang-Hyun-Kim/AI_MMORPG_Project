#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
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

private:
	FSocket* Socket;
	TSharedPtr<FNetworkWorker> NetworkWorker;
};
