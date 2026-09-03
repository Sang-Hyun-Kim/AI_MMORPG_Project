#include "ClientPacketHandler.h"
#include "../AMC1.h" // For UE_LOG, etc.
#include "../AMC1GameInstance.h"
#include "../Manager/AMC1ObjectManager.h"
#include "Engine/Engine.h"

std::array<PacketHandlerFunc, UINT16_MAX + 1> GPacketHandler; // [S2] 65536칸
TWeakObjectPtr<UAMC1GameInstance> ClientPacketHandler::GGameInstance = nullptr;

bool Handle_INVALID(PacketSessionRef& session, std::span<std::byte> buffer)
{
	UE_LOG(LogTemp, Error, TEXT("[PacketHandler] Handle_INVALID called."));
	return false;
}

bool Handle_S_LOGIN(PacketSessionRef& session, Protocol::S_LOGIN& pkt)
{
	UE_LOG(LogTemp, Log, TEXT("[PacketHandler] S_LOGIN Received! Success: %d"), pkt.success());
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Cyan, FString::Printf(TEXT("[Packet] S_LOGIN (Success: %d)"), pkt.success()));

	if (pkt.success())
	{
		UAMC1GameInstance* GI = ClientPacketHandler::GGameInstance.Get();
		if (GI)
		{
			Protocol::C_ENTER_GAME enterPkt;
			enterPkt.set_playerindex(0); // 첫 번째 캐릭터로 자동 접속 시뮬레이션
			SendBufferRef sendBuf = ClientPacketHandler::MakeSendBuffer(enterPkt);
			GI->SendPacket(sendBuf);
			UE_LOG(LogTemp, Log, TEXT("[PacketHandler] C_ENTER_GAME Sent automatically"));
		}
	}

	return true;
}

bool Handle_S_ENTER_GAME(PacketSessionRef& session, Protocol::S_ENTER_GAME& pkt)
{
	UE_LOG(LogTemp, Log, TEXT("[PacketHandler] S_ENTER_GAME Received! Success: %d"), pkt.success());

	if (pkt.success())
	{
		UAMC1GameInstance* GI = ClientPacketHandler::GGameInstance.Get();
		if (GI)
		{
			UAMC1ObjectManager* ObjManager = GI->GetSubsystem<UAMC1ObjectManager>();
			if (ObjManager)
			{
				uint64 MyId = pkt.player().objectinfo().objectid();
				ObjManager->SetMyPlayerId(MyId);
				UE_LOG(LogTemp, Log, TEXT("[PacketHandler] My Player ID set to %llu"), MyId);

				// Key=1 고정 라인으로 10초간 청색 입장 성공 메시지 표시
				if (GEngine)
				{
					GEngine->AddOnScreenDebugMessage(1, 10.f, FColor::Cyan, 
						FString::Printf(TEXT("★ [Packet] S_ENTER_GAME Connected! MyPlayerId: %llu ★"), MyId));
				}
			}
		}
	}
	return true;
}

bool Handle_S_LEAVE_GAME(PacketSessionRef& session, Protocol::S_LEAVE_GAME& pkt)
{
	return true;
}

bool Handle_S_SPAWN(PacketSessionRef& session, Protocol::S_SPAWN& pkt)
{
	UAMC1GameInstance* GI = ClientPacketHandler::GGameInstance.Get();
	if (!GI) return true;
	UAMC1ObjectManager* ObjManager = GI->GetSubsystem<UAMC1ObjectManager>();
	if (!ObjManager) return true;

	for (int i = 0; i < pkt.objects_size(); i++)
	{
		const Protocol::ObjectInfo& info = pkt.objects(i);
		// 나 자신은 프록시로 스폰하지 않음
		if (info.objectid() != ObjManager->GetMyPlayerId())
		{
			FVector Loc(info.posinfo().x(), info.posinfo().y(), info.posinfo().z());
			ObjManager->SpawnProxy(info.objectid(), Loc, info.posinfo().yaw());
		}
	}
	return true;
}

bool Handle_S_DESPAWN(PacketSessionRef& session, Protocol::S_DESPAWN& pkt)
{
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Cyan, TEXT("[Packet] S_DESPAWN"));

	UAMC1GameInstance* GI = ClientPacketHandler::GGameInstance.Get();
	if (!GI) return true;
	UAMC1ObjectManager* ObjManager = GI->GetSubsystem<UAMC1ObjectManager>();
	if (!ObjManager) return true;

	for (int i = 0; i < pkt.objectids_size(); i++)
	{
		ObjManager->DespawnProxy(pkt.objectids(i));
	}
	return true;
}

bool Handle_S_MOVE(PacketSessionRef& session, Protocol::S_MOVE& pkt)
{
	UAMC1GameInstance* GI = ClientPacketHandler::GGameInstance.Get();
	if (!GI) return true;
	UAMC1ObjectManager* ObjManager = GI->GetSubsystem<UAMC1ObjectManager>();
	if (!ObjManager) return true;

	if (pkt.objectid() != ObjManager->GetMyPlayerId())
	{
		FVector Loc(pkt.posinfo().x(), pkt.posinfo().y(), pkt.posinfo().z());
		ObjManager->UpdateProxyTransform(pkt.objectid(), Loc, pkt.posinfo().yaw());

		// 상대방 프록시 전용 라인(Key = 200 + ObjectId)에서 초록색으로 좌표 실시간 갱신 (화면 도배 없음)
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(200 + static_cast<int32>(pkt.objectid() % 100), 2.0f, FColor::Green,
				FString::Printf(TEXT("[Recv] S_MOVE [Proxy: %llu] Pos: (%.1f, %.1f, %.1f)"),
					pkt.objectid(), Loc.X, Loc.Y, Loc.Z));
		}
	}
	return true;
}

bool Handle_S_CHAT(PacketSessionRef& session, Protocol::S_CHAT& pkt)
{
	UE_LOG(LogTemp, Log, TEXT("[PacketHandler] S_CHAT Received!"));
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Cyan, TEXT("[Packet] S_CHAT"));
	return true;
}

bool Handle_S_PONG(PacketSessionRef& session, Protocol::S_PONG& pkt)
{
	UE_LOG(LogTemp, Log, TEXT("[PacketHandler] S_PONG Received!"));
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Cyan, TEXT("[Packet] S_PONG"));
	return true;
}

bool Handle_S_ATTACK(PacketSessionRef& session, Protocol::S_ATTACK& pkt)
{
	UE_LOG(LogTemp, Log, TEXT("[PacketHandler] S_ATTACK Received!"));
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Cyan, TEXT("[Packet] S_ATTACK"));
	return true;
}

bool Handle_S_STATUS_CHANGE(PacketSessionRef& session, Protocol::S_STATUS_CHANGE& pkt)
{
	UE_LOG(LogTemp, Log, TEXT("[PacketHandler] S_STATUS_CHANGE Received!"));
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Cyan, TEXT("[Packet] S_STATUS_CHANGE"));
	return true;
}
