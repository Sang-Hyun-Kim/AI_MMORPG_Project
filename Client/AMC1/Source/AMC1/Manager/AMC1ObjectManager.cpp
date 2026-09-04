#include "AMC1ObjectManager.h"
#include "../AMC1Character.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "UObject/ConstructorHelpers.h"
#include "Components/CapsuleComponent.h"
// [2026-09-04] F10 좌표 복원에 필요한 헤더
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"

void UAMC1ObjectManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogTemp, Log, TEXT("[UAMC1ObjectManager] Initialized"));
}

void UAMC1ObjectManager::Deinitialize()
{
	// 보류 타이머가 남아 있으면 정리합니다. (월드가 사라진 뒤 콜백이 도는 것을 방지)
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PendingTransformTimer);
	}
	bHasPendingTransform = false;

	ProxyCharacters.Empty();
	Super::Deinitialize();
}

/*
 * ResetForNewLevel — 레벨 전환 시 상태 정리 (2026-09-04 신설 / T5)
 *
 * 이 서브시스템은 GameInstance와 함께 레벨 전환을 살아남지만, 아래 상태들은
 * 레벨과 함께 죽거나 무효화됩니다. 자세한 배경은 헤더의 선언부 주석을 보십시오.
 *
 * 호출부: UAMC1GameInstance::HandlePostLoadMap
 */
void UAMC1ObjectManager::ResetForNewLevel()
{
	/*
	 * 1) 보류 재시도 타이머
	 *    옛 월드의 TimerManager에 묶여 있어 전환 후에는 절대 돌지 않습니다.
	 *    ClearTimer는 새 월드의 매니저를 대상으로 하므로 사실상 무해하지만,
	 *    핸들 자체를 무효화해 두어야 이후 SetTimer가 깨끗하게 새로 잡힙니다.
	 */
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PendingTransformTimer);
	}
	PendingTransformTimer.Invalidate();

	/*
	 * 2) 보류 좌표
	 *    이전 세션에서 적용하지 못한 좌표가 남아 있으면 새 레벨의 폰에
	 *    엉뚱하게 적용됩니다. 새 레벨에서는 서버가 S_ENTER_GAME으로
	 *    좌표를 다시 보내주므로 버리는 것이 옳습니다.
	 */
	bHasPendingTransform = false;
	PendingLocation = FVector::ZeroVector;
	PendingYaw = 0.f;

	/*
	 * 3) 프록시 맵
	 *    액터는 레벨과 함께 파괴되었고 UPROPERTY라 포인터는 GC가 null로 만들지만
	 *    **키는 그대로 남습니다.** 그 상태로 재입장하면 SpawnProxy가
	 *    "이미 있다"고 판단해 스폰을 건너뛰어 다른 플레이어가 보이지 않습니다.
	 *    새 레벨에서는 서버가 S_SPAWN으로 현재 방의 인원을 다시 알려주므로
	 *    비우는 것이 정답입니다.
	 */
	const int32 StaleCount = ProxyCharacters.Num();
	ProxyCharacters.Empty();

	UE_LOG(LogTemp, Log,
		TEXT("[ObjectManager] ResetForNewLevel: cleared %d proxy entries, pending transform dropped."),
		StaleCount);

	// MyPlayerId는 유지합니다 — 같은 사람이 같은 세션으로 레벨을 옮기는 것이므로.
	// (로그아웃 후 다른 계정 로그인을 지원하게 되면 그때 초기화가 필요합니다.)
}

/*
 * ApplyMyPlayerTransform — 결함 F10 대응
 * 서버가 S_ENTER_GAME에 실어 보낸 "DB에 저장돼 있던 마지막 좌표"를 내 폰에 적용합니다.
 * 자세한 경위는 AMC1ObjectManager.h의 선언부 주석을 참고하세요.
 */
void UAMC1ObjectManager::ApplyMyPlayerTransform(const FVector& Location, float Yaw)
{
	if (ApplyTransformToLocalPawn(Location, Yaw))
		return;

	/*
	 * 폰이 아직 없습니다(레벨 로드/Possess 이전에 패킷이 도착한 경우).
	 * 값을 보류해 두고 짧은 주기로 재시도합니다. 적용에 성공하면 타이머를 해제합니다.
	 * 그냥 포기하면 왕복이 타이밍에 따라 보였다 안 보였다 하게 됩니다.
	 */
	bHasPendingTransform = true;
	PendingLocation = Location;
	PendingYaw = Yaw;

	UE_LOG(LogTemp, Log,
		TEXT("[UAMC1ObjectManager] Local pawn not ready. Pending spawn transform (%s, Yaw=%.1f)."),
		*Location.ToString(), Yaw);

	if (UWorld* World = GetWorld())
	{
		if (!World->GetTimerManager().IsTimerActive(PendingTransformTimer))
		{
			World->GetTimerManager().SetTimer(
				PendingTransformTimer, this,
				&UAMC1ObjectManager::TryFlushPendingTransform,
				0.1f, /*bLoop=*/true);
		}
	}
}

void UAMC1ObjectManager::TryFlushPendingTransform()
{
	if (!bHasPendingTransform)
	{
		if (UWorld* World = GetWorld())
			World->GetTimerManager().ClearTimer(PendingTransformTimer);
		return;
	}

	if (ApplyTransformToLocalPawn(PendingLocation, PendingYaw))
	{
		bHasPendingTransform = false;
		if (UWorld* World = GetWorld())
			World->GetTimerManager().ClearTimer(PendingTransformTimer);
	}
}

bool UAMC1ObjectManager::ApplyTransformToLocalPawn(const FVector& Location, float Yaw)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
		return false;

	APlayerController* PC = World->GetFirstPlayerController();
	if (PC == nullptr)
		return false;

	APawn* Pawn = PC->GetPawn();
	if (Pawn == nullptr)
		return false;

	/*
	 * TeleportTo가 아니라 SetActorLocation(+ Sweep=false)을 씁니다.
	 * 저장된 좌표가 지오메트리와 겹칠 경우 TeleportTo는 실패하거나 위치를 보정해
	 * "DB에 있던 그 자리"가 아니게 될 수 있는데, 왕복 증명에서는 값이 그대로
	 * 재현되는 것이 중요합니다.
	 */
	Pawn->SetActorLocation(Location, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);

	// 회전은 컨트롤러가 소유하므로 컨트롤 로테이션도 함께 맞춰야 카메라가 튀지 않습니다.
	FRotator NewRotation(0.f, Yaw, 0.f);
	Pawn->SetActorRotation(NewRotation);
	PC->SetControlRotation(NewRotation);

	/*
	 * [P2-3 / 2026-09-04 5차] 화면 메시지와 **같은 문자열**을 로그 파일에도 남깁니다.
	 *
	 *   이전에는 좌표 복원 사실이 AddOnScreenDebugMessage 로만 표시되어
	 *   Saved/Logs/AMC1.log 에 흔적이 남지 않았습니다. 그래서 "복원이 됐는가"를
	 *   판별하려면 PIE 화면을 사람이 직접 봐야 했습니다(2026-09-04 진단 시 실제로 겪음).
	 *   EC2 원격 시험처럼 화면을 볼 수 없는 상황에서는 판별 자체가 불가능해집니다.
	 *
	 *   바로 아래 "Applied saved transform" 로그는 이미 있었지만 태그가 달라
	 *   화면에서 본 문구(★ [Restore])로 로그를 검색하면 아무것도 걸리지 않았습니다.
	 *   두 표현을 일치시켜 화면 관측과 로그 검색이 같은 키워드로 맞물리게 합니다.
	 */
	UE_LOG(LogTemp, Log,
		TEXT("★ [Restore] Loaded position from DB: %.0f, %.0f, %.0f (Yaw=%.1f) ★"),
		Location.X, Location.Y, Location.Z, Yaw);

	UE_LOG(LogTemp, Log,
		TEXT("[UAMC1ObjectManager] Applied saved transform to local pawn: %s (Yaw=%.1f)"),
		*Location.ToString(), Yaw);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(3, 10.f, FColor::Magenta,
			FString::Printf(TEXT("★ [Restore] Loaded position from DB: %.0f, %.0f, %.0f ★"),
				Location.X, Location.Y, Location.Z));
	}

	return true;
}

void UAMC1ObjectManager::SpawnProxy(uint64 ObjectId, FVector Location, float Yaw)
{
	if (ProxyCharacters.Contains(ObjectId))
		return;

	UWorld* World = GetWorld();
	if (!World) return;

	// MCP로 생성할 프록시 전용 블루프린트 에셋 로드 (기본 도형 장착용)
	UClass* ProxyClass = LoadClass<AAMC1Character>(nullptr, TEXT("/Game/AMC1/Blueprints/BP_AMC1Proxy.BP_AMC1Proxy_C"));
	
	if (!ProxyClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[UAMC1ObjectManager] BP_AMC1Proxy_C not found. Fallback to AAMC1Character."));
		ProxyClass = AAMC1Character::StaticClass();
	}

	// 중앙값(Location) 기준 ObjectId 기반 원형 분산 오프셋 부여 (모델 겹침 방지: 60도 간격, 반경 150)
	float Angle = static_cast<float>(ObjectId % 6) * (2.0f * PI / 6.0f);
	float Radius = 150.0f;
	FVector SpawnLocation = Location + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.0f);

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	FRotator Rotation(0.0f, Yaw, 0.0f);
	AAMC1Character* NewProxy = World->SpawnActor<AAMC1Character>(ProxyClass, SpawnLocation, Rotation, SpawnParams);

	if (NewProxy)
	{
		// 프록시 캐릭터는 로컬 플레이어와의 물리 밀침(Depenetration)을 방지하기 위해 Pawn 충돌 채널을 무시(Ignore)
		if (UCapsuleComponent* Capsule = NewProxy->GetCapsuleComponent())
		{
			Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		}

		// 프록시는 언리얼 내장 리플리케이션이 아닌 C++ IOCP 소켓 동기화에만 반응
		NewProxy->SetReplicates(false);

		ProxyCharacters.Add(ObjectId, NewProxy);
		UE_LOG(LogTemp, Log, TEXT("[UAMC1ObjectManager] Spawned Proxy for %llu at %s (Base: %s)"), ObjectId, *SpawnLocation.ToString(), *Location.ToString());
	}
}

void UAMC1ObjectManager::DespawnProxy(uint64 ObjectId)
{
	AAMC1Character** ProxyPtr = ProxyCharacters.Find(ObjectId);
	if (ProxyPtr && *ProxyPtr)
	{
		(*ProxyPtr)->Destroy();
		ProxyCharacters.Remove(ObjectId);
		UE_LOG(LogTemp, Log, TEXT("[UAMC1ObjectManager] Despawned Proxy %llu"), ObjectId);
	}
}

void UAMC1ObjectManager::UpdateProxyTransform(uint64 ObjectId, FVector Location, float Yaw)
{
	AAMC1Character** ProxyPtr = ProxyCharacters.Find(ObjectId);
	if (ProxyPtr && *ProxyPtr)
	{
		FRotator Rotation(0.0f, Yaw, 0.0f);
		// 임시로 순간이동(SetActorLocation) 처리. 추후 VInterpTo 등으로 보간 가능.
		(*ProxyPtr)->SetActorLocationAndRotation(Location, Rotation);
	}
}
