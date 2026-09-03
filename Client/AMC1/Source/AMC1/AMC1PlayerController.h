#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "AMC1PlayerController.generated.h"

UCLASS()
class AMC1_API AAMC1PlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	
	UFUNCTION(BlueprintCallable, Category="Network")
	void SendMovePacket();

	UFUNCTION(BlueprintCallable, Category="Network")
	void SendAttackPacket(int32 TargetId);
};
