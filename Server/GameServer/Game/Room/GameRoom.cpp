#include "GameRoom.h"
#include "ServerPacketHandler.h"
#include "JobTimer.h"
#include "CoreGlobal.h"

GameRoom::GameRoom()
{
}

GameRoom::~GameRoom()
{
}

void GameRoom::Init()
{
	DoAsync(&GameRoom::Update);
}

void GameRoom::Update()
{
	// 50ms마다 틱 실행 (JobTimer 이용)
	// TODO: 몬스터 AI 틱 처리 및 패킷 Flush 처리

	// 다음 틱 예약 (자신이 소속된 큐에 예약)
	// GJobTimer->Reserve를 감싸는 유틸을 구현하거나 직접 호출
	// GJobTimer는 GlobalTimer로서 tick count 기반 동작
	DoAsync(&GameRoom::Update);
	// 실전에서는 DoTimer(50, &GameRoom::Update) 형태로 예약해야 함.
	// 현재 JobTimer는 Global 단위이므로 임시로 Sleep이나 틱 기반 로직 처리.
}

void GameRoom::Enter(GameObjectRef gameObject)
{
	if (gameObject == nullptr)
		return;

	PlayerRef player = std::static_pointer_cast<Player>(gameObject);
	uint64 objectId = player->GetObjectId();
	
	_players[objectId] = player;
	player->SetRoom(static_pointer_cast<GameRoom>(shared_from_this()));

	// S_SPAWN 브로드캐스트
	Protocol::S_SPAWN spawnPkt;
	Protocol::ObjectInfo* info = spawnPkt.add_objects();
	info->CopyFrom(*player->GetObjectInfo());

	SendBufferRef sendBuffer = ServerPacketHandler::MakeSendBuffer(spawnPkt);
	BroadcastToAdjacentSectors(player->GetPosInfo()->x(), player->GetPosInfo()->y(), sendBuffer);

	// 본인에게는 현재 방에 있는 유저들 정보를 전송
	Protocol::S_SPAWN mySpawnPkt;
	for (auto& pair : _players)
	{
		if (pair.first != objectId)
		{
			Protocol::ObjectInfo* pInfo = mySpawnPkt.add_objects();
			pInfo->CopyFrom(*pair.second->GetObjectInfo());
		}
	}

	if (mySpawnPkt.objects_size() > 0)
	{
		SendBufferRef mySpawnBuffer = ServerPacketHandler::MakeSendBuffer(mySpawnPkt);
		player->Send(mySpawnBuffer);
	}
}

void GameRoom::Leave(GameObjectRef gameObject)
{
	if (gameObject == nullptr)
		return;

	uint64 objectId = gameObject->GetObjectId();
	if (_players.erase(objectId) == 0)
		return;

	gameObject->SetRoom(nullptr);

	Protocol::S_DESPAWN despawnPkt;
	despawnPkt.add_objectids(objectId);

	SendBufferRef sendBuffer = ServerPacketHandler::MakeSendBuffer(despawnPkt);
	Broadcast(sendBuffer); // 임시로 전체 브로드캐스트
}

void GameRoom::HandleMove(PlayerRef player, Protocol::C_MOVE pkt)
{
	if (player == nullptr)
		return;

	// 위치 갱신 (서버 검증 추가 가능)
	player->GetPosInfo()->CopyFrom(pkt.posinfo());

	// 주변 브로드캐스팅
	Protocol::S_MOVE movePkt;
	movePkt.set_objectid(player->GetObjectId());
	movePkt.mutable_posinfo()->CopyFrom(pkt.posinfo());

	SendBufferRef sendBuffer = ServerPacketHandler::MakeSendBuffer(movePkt);
	BroadcastToAdjacentSectors(player->GetPosInfo()->x(), player->GetPosInfo()->y(), sendBuffer);
}

void GameRoom::Broadcast(SendBufferRef sendBuffer)
{
	for (auto& pair : _players)
	{
		pair.second->Send(sendBuffer);
	}
}

// 간단한 Sector Grid (거리 기반으로 타협)
// x, y 좌표를 1000 단위로 나누어 인덱싱
int32 GameRoom::GetSectorIndex(float x, float y)
{
	// 예시: 1000 단위 구역
	return static_cast<int32>(x / 1000.f) + static_cast<int32>(y / 1000.f) * 1000;
}

std::vector<PlayerRef> GameRoom::GetAdjacentSectorPlayers(float x, float y)
{
	std::vector<PlayerRef> adjacentPlayers;
	// 지금은 간단한 O(N) 거리 비교로 대체 (Sector 자료구조 맵핑 전)
	// 반경 2000 안의 유저를 반환
	const float AOI_RADIUS_SQ = 2000.0f * 2000.0f;

	for (auto& pair : _players)
	{
		float dx = pair.second->GetPosInfo()->x() - x;
		float dy = pair.second->GetPosInfo()->y() - y;
		if (dx * dx + dy * dy <= AOI_RADIUS_SQ)
		{
			adjacentPlayers.push_back(pair.second);
		}
	}

	return adjacentPlayers;
}

void GameRoom::BroadcastToAdjacentSectors(float x, float y, SendBufferRef sendBuffer)
{
	std::vector<PlayerRef> players = GetAdjacentSectorPlayers(x, y);
	for (PlayerRef player : players)
	{
		player->Send(sendBuffer);
	}
}
