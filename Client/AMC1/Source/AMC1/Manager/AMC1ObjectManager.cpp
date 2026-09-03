#include "AMC1ObjectManager.h"
#include "../AMC1Character.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "UObject/ConstructorHelpers.h"
#include "Components/CapsuleComponent.h"

void UAMC1ObjectManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogTemp, Log, TEXT("[UAMC1ObjectManager] Initialized"));
}

void UAMC1ObjectManager::Deinitialize()
{
	ProxyCharacters.Empty();
	Super::Deinitialize();
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
