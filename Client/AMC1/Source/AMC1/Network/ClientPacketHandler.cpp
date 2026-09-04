#include "ClientPacketHandler.h"
#include "../AMC1.h" // For UE_LOG, etc.
#include "ClientPacketSession.h"
#include "../AMC1GameInstance.h"
#include "../Manager/AMC1ObjectManager.h"
#include "Engine/Engine.h"

std::array<PacketHandlerFunc, UINT16_MAX + 1> GPacketHandler; // [S2] 65536칸

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
		UAMC1GameInstance* GI = GetGameInstanceFrom(session);
		if (GI)
		{
			/*
			 * [2026-09-04 / T4] C_ENTER_GAME 송신을 GameInstance로 이관했습니다.
			 *
			 * 변경 전: 이 자리에서 곧바로 C_ENTER_GAME을 보냈습니다.
			 *   레벨이 하나뿐일 때는 문제가 없었지만, 로그인 레벨 -> 게임 레벨
			 *   전환을 끼워 넣으면 깨집니다. 서버의 S_ENTER_GAME(좌표)이
			 *   **레벨 로드보다 먼저 도착**해 적용할 폰이 없기 때문입니다.
			 *
			 * 변경 후: GameInstance가 "레벨 전환이 필요한가"를 판단하고,
			 *   필요하면 레벨을 열고 **로드와 폰 Possess가 끝난 뒤에** 보냅니다.
			 *   GameLevelName이 비어 있으면(현재 기본값) 곧바로 보내므로
			 *   기존 단일 레벨 동작과 동일합니다.
			 *
			 * ⚠️ 여기로 송신 코드를 되돌리지 마십시오. 레벨 전환이 도입되는 순간
			 *    좌표 복원이 타이밍에 따라 들쭉날쭉해집니다(F10과 같은 유형).
			 */
			GI->OnAuthenticatedEnterWorld();
		}
	}

	return true;
}

bool Handle_S_ENTER_GAME(PacketSessionRef& session, Protocol::S_ENTER_GAME& pkt)
{
	UE_LOG(LogTemp, Log, TEXT("[PacketHandler] S_ENTER_GAME Received! Success: %d"), pkt.success());

	if (pkt.success())
	{
		UAMC1GameInstance* GI = GetGameInstanceFrom(session);
		if (GI)
		{
			UAMC1ObjectManager* ObjManager = GI->GetSubsystem<UAMC1ObjectManager>();
			if (ObjManager)
			{
				uint64 MyId = pkt.player().objectinfo().objectid();
				ObjManager->SetMyPlayerId(MyId);
				UE_LOG(LogTemp, Log, TEXT("[PacketHandler] My Player ID set to %llu"), MyId);

				/*
				 * [2026-09-04] 저장된 좌표 복원 — 결함 F10
				 *
				 * 변경 전: 이 핸들러는 objectid()만 꺼내 쓰고 **좌표를 읽지도 않았습니다.**
				 *   서버는 S_ENTER_GAME에 MakePlayerInfo()로 조립한 좌표(=DB에서 읽어온
				 *   마지막 위치)를 실어 보내고 있었는데, 클라이언트가 그것을 버렸습니다.
				 *   그래서 내 캐릭터는 언제나 레벨의 PlayerStart에서 시작했고,
				 *   "이동해도 매번 같은 자리에서 스폰"으로 보였습니다.
				 *   남의 캐릭터를 그리는 Handle_S_SPAWN은 좌표를 제대로 꺼내 쓰고 있었으므로,
				 *   프록시 경로만 구현되고 본인 경로가 빠진 비대칭이었습니다.
				 *
				 * 변경 후: 받은 좌표를 내 폰에 적용합니다.
				 *   폰이 아직 없으면(레벨 로드/Possess 이전 도착) ObjectManager가 값을
				 *   보류했다가 준비되는 즉시 적용합니다.
				 *
				 * ⚠️ 스레드 안전성: 이 핸들러는 FNetworkWorker::Run()에서
				 *   AsyncTask(ENamedThreads::GameThread, ...)로 디스패치된 뒤 실행되므로
				 *   **이미 게임 스레드 위**입니다. 그래서 여기서 액터를 조작해도
				 *   check(IsInGameThread()) assertion에 걸리지 않습니다.
				 *   그 디스패치 구조를 제거하면 이 코드가 즉시 크래시합니다.
				 */
				const Protocol::PositionInfo& Pos = pkt.player().objectinfo().posinfo();
				ObjManager->ApplyMyPlayerTransform(
					FVector(Pos.x(), Pos.y(), Pos.z()), Pos.yaw());

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
	UAMC1GameInstance* GI = GetGameInstanceFrom(session);
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

	UAMC1GameInstance* GI = GetGameInstanceFrom(session);
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
	UAMC1GameInstance* GI = GetGameInstanceFrom(session);
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
