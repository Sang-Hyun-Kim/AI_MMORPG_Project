#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "AMC1ObjectManager.generated.h"

class AAMC1Character;

/**
 * 멀티플레이어 환경에서 다른 유저들의 프록시 캐릭터를 스폰하고 위치를 동기화하는 매니저
 */
UCLASS()
class AMC1_API UAMC1ObjectManager : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// 프록시 관리 함수
	void SpawnProxy(uint64 ObjectId, FVector Location, float Yaw);
	void DespawnProxy(uint64 ObjectId);
	void UpdateProxyTransform(uint64 ObjectId, FVector Location, float Yaw);
	
	// 유저 본인 추적 (S_ENTER_GAME에서 세팅)
	void SetMyPlayerId(uint64 InId) { MyPlayerId = InId; }
	uint64 GetMyPlayerId() const { return MyPlayerId; }

private:
	uint64 MyPlayerId = 0;

	UPROPERTY()
	TMap<uint64, AAMC1Character*> ProxyCharacters;
};
