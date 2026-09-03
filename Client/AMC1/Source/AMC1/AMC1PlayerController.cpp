#include "AMC1PlayerController.h"
#include "Network/ClientPacketHandler.h"
#include "AMC1GameInstance.h"
#include "Manager/AMC1ObjectManager.h"

void AAMC1PlayerController::BeginPlay()
{
	Super::BeginPlay();
}

void AAMC1PlayerController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void AAMC1PlayerController::SendMovePacket()
{
	APawn* MyPawn = GetPawn();
	if (!MyPawn) return;

	FVector Location = MyPawn->GetActorLocation();
	float Yaw = MyPawn->GetControlRotation().Yaw;

	UAMC1GameInstance* GameInstance = Cast<UAMC1GameInstance>(GetGameInstance());
	if (GameInstance)
	{
		Protocol::C_MOVE pkt;
		Protocol::PositionInfo* posInfo = pkt.mutable_posinfo();
		posInfo->set_x(Location.X);
		posInfo->set_y(Location.Y);
		posInfo->set_z(Location.Z);
		posInfo->set_yaw(Yaw);

		SendBufferRef sendBuffer = ClientPacketHandler::MakeSendBuffer(pkt);
		GameInstance->SendPacket(sendBuffer);

		uint64 MyId = 0;
		if (UAMC1ObjectManager* ObjMgr = GameInstance->GetSubsystem<UAMC1ObjectManager>())
		{
			MyId = ObjMgr->GetMyPlayerId();
		}

		UE_LOG(LogTemp, Log, TEXT("[PlayerController] C_MOVE Sent [Player: %llu]: (X: %f, Y: %f, Z: %f, Yaw: %f)"), MyId, Location.X, Location.Y, Location.Z, Yaw);

		// Key=100 고정 라인에서 내 이동 좌표 실시간 갱신 (화면 도배 원천 방지)
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(100, 2.0f, FColor::Yellow, 
				FString::Printf(TEXT("[Send] C_MOVE [Player: %llu] Pos: (%.1f, %.1f, %.1f)"), MyId, Location.X, Location.Y, Location.Z));
		}
	}
}

void AAMC1PlayerController::SendAttackPacket(int32 TargetId)
{
	UAMC1GameInstance* GameInstance = Cast<UAMC1GameInstance>(GetGameInstance());
	if (GameInstance)
	{
		Protocol::C_ATTACK pkt;
		pkt.set_targetid(TargetId);

		SendBufferRef sendBuffer = ClientPacketHandler::MakeSendBuffer(pkt);
		GameInstance->SendPacket(sendBuffer);

		UE_LOG(LogTemp, Log, TEXT("[PlayerController] C_ATTACK Sent: (TargetId: %d)"), TargetId);
		if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Yellow, FString::Printf(TEXT("[Send] C_ATTACK: (TargetId: %d)"), TargetId));

	}
}
