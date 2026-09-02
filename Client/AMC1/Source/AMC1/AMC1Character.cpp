#include "AMC1Character.h"

AAMC1Character::AAMC1Character()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AAMC1Character::BeginPlay()
{
	Super::BeginPlay();
}

void AAMC1Character::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void AAMC1Character::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
}
