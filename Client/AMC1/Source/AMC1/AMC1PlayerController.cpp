#include "AMC1PlayerController.h"
#include "Network/ClientPacketHandler.h"
#include "AMC1GameInstance.h"

void AAMC1PlayerController::BeginPlay()
{
	Super::BeginPlay();
}

void AAMC1PlayerController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void AAMC1PlayerController::SendMovePacket(FVector Location, float Yaw)
{
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
	}
}
