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

	/*
	 * ApplyMyPlayerTransform — 결함 F10 대응 (2026-09-04 신설)
	 *
	 * [무엇이 문제였나]
	 *   Handle_S_ENTER_GAME은 SetMyPlayerId()만 호출하고, 패킷에 실려 온
	 *   posinfo(서버가 DB에서 읽어 보낸 마지막 좌표)를 **읽지도 않았습니다.**
	 *   그래서 서버·DB를 아무리 고쳐도 내 캐릭터는 언제나 레벨의 PlayerStart에서
	 *   시작했고, "이동해도 매번 같은 자리"로 보였습니다.
	 *   흥미롭게도 남의 캐릭터를 그리는 Handle_S_SPAWN은 좌표를 정확히 꺼내
	 *   SpawnProxy에 넘기고 있었습니다 — 프록시 경로만 구현되고 본인 경로가
	 *   빠진 비대칭이었습니다.
	 *
	 * [타이밍 문제와 해법]
	 *   S_ENTER_GAME은 레벨 로드/Possess보다 **먼저 도착할 수 있습니다.**
	 *   그 순간 폰이 없으므로 그냥 SetActorLocation을 부르면 조용히 실패하고,
	 *   결과적으로 왕복이 "보였다 안 보였다" 합니다 — 데모에서 가장 피해야 할
	 *   종류의 비결정성입니다.
	 *   그래서 폰이 없으면 값을 보류(Pending)해 두고, 폰이 준비되는 즉시 1회 적용합니다.
	 *   순서와 무관하게 항상 성립합니다.
	 */
	void ApplyMyPlayerTransform(const FVector& Location, float Yaw);

	/** 보류된 좌표가 있으면 지금 적용을 시도합니다. 폰이 준비된 뒤 호출됩니다. */
	void TryFlushPendingTransform();

private:
	/** 보류된 좌표를 실제 폰에 적용합니다. 성공하면 true. */
	bool ApplyTransformToLocalPawn(const FVector& Location, float Yaw);

private:
	uint64 MyPlayerId = 0;

	// S_ENTER_GAME이 Possess보다 먼저 도착한 경우를 위한 보류 값
	bool    bHasPendingTransform = false;
	FVector PendingLocation = FVector::ZeroVector;
	float   PendingYaw = 0.f;

	// 보류 값을 재시도하기 위한 타이머. 적용에 성공하면 해제합니다.
	FTimerHandle PendingTransformTimer;

	UPROPERTY()
	TMap<uint64, AAMC1Character*> ProxyCharacters;
};
