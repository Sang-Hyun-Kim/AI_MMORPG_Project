#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AMC1Character.generated.h"

UCLASS()
class AMC1_API AAMC1Character : public ACharacter
{
	GENERATED_BODY()

public:
	AAMC1Character();

protected:
	virtual void BeginPlay() override;

public:	
	virtual void Tick(float DeltaTime) override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
};
