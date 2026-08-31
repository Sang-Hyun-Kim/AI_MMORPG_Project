#include "GameRoom.h"
#include "ServerPacketHandler.h"
#include "JobTimer.h"
#include "CoreGlobal.h"
#include "RedisManager.h"
#include "DBConnectionPool.h"
#include "DBAwaitable.h"
#include <sw/redis++/redis++.h>

GameRoom::GameRoom()
{
}

GameRoom::~GameRoom()
{
}

void GameRoom::Init()
{
	// 최초 틱 예약: 50ms 후 첫 Update 실행
	DoTimer(50, &GameRoom::Update);

	// 오토 세이브 예약: 1분(60000ms) 후 실행 (테스트용)
	DoTimer(60000, &GameRoom::AutoSave);
}

void GameRoom::Update()
{
	// 50ms마다 틱 실행 (JobTimer 기반 예약)
	// TODO: 몬스터 AI 틱 처리 및 패킷 Flush 처리

	// 다음 틱을 50ms 후로 예약 (스레드를 즉시 반납)
	// GJobTimer의 Distribute 주기(현재 100ms)에 따라 실제 실행은 50~150ms 범위
	DoTimer(50, &GameRoom::Update);
}

void GameRoom::AutoSave()
{
	for (auto& pair : _players)
	{
		PlayerSaveData snapshot = pair.second->GetSaveData();
		DoAsync(&GameRoom::SavePlayerToDB, snapshot);
	}

	std::cout << "[GameRoom] AutoSave Triggered for " << _players.size() << " players." << std::endl;

	DoTimer(60000, &GameRoom::AutoSave);
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

	PlayerRef player = std::static_pointer_cast<Player>(gameObject);
	if (player)
	{
		// DB 스냅샷 저장 (Leave 이전에 복사해야 안전)
		PlayerSaveData snapshot = player->GetSaveData();
		DoAsync(&GameRoom::SavePlayerToDB, snapshot);
	}

	uint64 objectId = gameObject->GetObjectId();
	if (_players.erase(objectId) == 0)
		return;

	gameObject->SetRoom(nullptr);

	Protocol::S_DESPAWN despawnPkt;
	despawnPkt.add_objectids(objectId);

	SendBufferRef sendBuffer = ServerPacketHandler::MakeSendBuffer(despawnPkt);
	Broadcast(sendBuffer); // 임시로 전체 브로드캐스트
}

JobTask GameRoom::SavePlayerToDB(PlayerSaveData data)
{
	auto jobQueue = shared_from_this();

	auto dbJob = [data](DBConnection* conn) {
		// 간단한 UPDATE 쿼리 조립 (향후 PreparedStatement 고려)
		std::string query = "UPDATE Player SET Gold = " + std::to_string(data.gold) + 
			", PosX = " + std::to_string(data.x) + 
			", PosY = " + std::to_string(data.y) + 
			", PosZ = " + std::to_string(data.z) + 
			" WHERE PlayerId = " + std::to_string(data.playerId) + ";";

		if (conn->Execute(query))
		{
			// 성공 시 콘솔 로깅 등
		}
	};

	co_await DBAwaitable(dbJob, jobQueue);

	std::cout << "Player " << data.name << " DB Save Complete (Coroutine Resumed)." << std::endl;
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

/*
 * HandleAttack (고블린 사냥 시나리오)
 * 기능: 유저가 몬스터(또는 허수아비)를 공격했을 때 처리하는 핸들러.
 * 데이터 흐름 (Data Flow):
 * 1. 클라이언트가 C_ATTACK 패킷 전송 (타겟 ID 포함)
 * 2. 서버가 타겟 사망 여부 판정 (데모에서는 무조건 사망한다고 가정)
 * 3. 클라이언트들에게 S_ATTACK 패킷 브로드캐스트 (타격 모션 재생용)
 * 4. 사망 시, 해당 유저에게 골드(+100) 지급 후 S_STATUS_CHANGE 브로드캐스트
 * 5. [중요] 획득된 골드 로그를 DB가 아닌 Redis Stream(gold_log)에 Publish (비동기 빠른 처리)
 */
void GameRoom::HandleAttack(PlayerRef player, Protocol::C_ATTACK pkt)
{
	if (player == nullptr) return;

	uint64 targetId = pkt.targetid();
	
	// 데모용: 타겟은 무조건 죽고 골드를 드랍한다고 임의 처리
	bool isKilled = true;
	int32 dropGold = 100;

	// 1. 공격 연출 브로드캐스팅
	Protocol::S_ATTACK attackPkt;
	attackPkt.set_objectid(player->GetObjectId());
	attackPkt.set_targetid(targetId);
	attackPkt.set_iskilled(isKilled);
	SendBufferRef attackBuf = ServerPacketHandler::MakeSendBuffer(attackPkt);
	BroadcastToAdjacentSectors(player->GetPosInfo()->x(), player->GetPosInfo()->y(), attackBuf);

	// 2. 골드 획득 처리 및 통보
	if (isKilled)
	{
		// Player 객체에 임시로 골드를 저장 (Player.h에 gold 멤버가 있다고 가정)
		player->AddGold(dropGold);

		Protocol::S_STATUS_CHANGE statusPkt;
		statusPkt.set_objectid(player->GetObjectId());
		statusPkt.set_gold(player->GetGold());
		SendBufferRef statusBuf = ServerPacketHandler::MakeSendBuffer(statusPkt);
		
		// 본인(혹은 시야 내 인원)에게 갱신된 골드 전송
		player->Send(statusBuf); 

		// 3. Redis Stream에 골드 획득 로그 발행 (Fire and Forget)
		if (GRedisManager && GRedisManager->GetRedis())
		{
			try {
				std::unordered_map<std::string, std::string> logData = {
					{"player_id", std::to_string(player->GetObjectId())},
					{"amount", std::to_string(dropGold)},
					{"reason", "goblin_hunt"}
				};
				// xadd를 사용하여 stream:gold_log 스트림에 로그 추가 (비동기 큐잉 역할)
				GRedisManager->GetRedis()->xadd("stream:gold_log", "*", logData.begin(), logData.end());
			} catch (const sw::redis::Error& e) {
				std::cout << "Redis XADD Error: " << e.what() << std::endl;
			}
		}
	}
}

/*
 * HandleCheckMailbox (웹 상점 연동 시나리오)
 * 기능: 웹 상점에서 결제 후, 인게임에서 우편함 수령을 요청했을 때의 처리
 * 특징: 직접 DB 쿼리가 필요하므로, 코루틴(ProcessMailboxDB)으로 비동기 작업을 넘깁니다.
 */
void GameRoom::HandleCheckMailbox(PlayerRef player, Protocol::C_CHECK_MAILBOX pkt)
{
	if (player == nullptr) return;

	// DB 조회를 시작 (비동기)
	DoAsync(&GameRoom::ProcessMailboxDB, player);
}

/*
 * ProcessMailboxDB (우편함 패턴 트랜잭션 - 비동기 DB 처리)
 * 기능: C# 서버가 결제 후 MySQL Mailbox 테이블에 넣어둔 아이템(골드)을 읽고 삭제(수령)합니다.
 * 데이터 흐름 (Data Flow):
 * 1. 게임 스레드에서 호출되면, co_await DBAwaitable을 만나 일시정지(Suspend) 됨.
 * 2. 백그라운드 DB 스레드가 MySQL에 "SELECT -> 수령 -> DELETE" 트랜잭션 수행.
 * 3. 완료 후 게임 스레드로 복귀(Resume)하여, 유저에게 아이템 획득 패킷(S_STATUS_CHANGE) 전송.
 * 
 * *주의*: 비동기 처리 도중 유저가 접속 종료(Leave)될 수 있으므로, 재개(Resume) 후 player 참조의 유효성을 체크해야 합니다. (이 예제에선 shared_ptr 캡처로 생명주기 연장)
 */
JobTask GameRoom::ProcessMailboxDB(PlayerRef player)
{
	auto jobQueue = shared_from_this(); // 원래 스레드로 돌아오기 위한 큐 저장
	uint64 playerId = player->GetObjectId(); // 추후 유효성 검증용
	
	// DB 워커 스레드에서 실행될 실제 작업 (람다 캡처로 데이터 복사)
	auto dbJob = [playerId](DBConnection* conn) {
		// 실제 상용 환경에선 트랜잭션(START TRANSACTION)과 함께 
		// SELECT * FROM Mailbox WHERE PlayerId = ? FOR UPDATE -> DELETE -> COMMIT
		// 이 이루어집니다. 
		// (본 데모에선 테이블 스키마가 없으므로 성공했다고 가정하고 로그만 남깁니다.)
		std::string query = "DELETE FROM Mailbox WHERE PlayerId = " + std::to_string(playerId) + " LIMIT 1;";
		conn->Execute(query);
	};

	// 1. 여기서 게임 스레드는 중단되고, DB 스레드로 dbJob이 넘어갑니다.
	co_await DBAwaitable(dbJob, jobQueue);

	// 2. DB 작업 완료 후, 다시 게임 스레드로 돌아왔습니다! (Resume)
	// (비동기 대기 중에 유저가 나갔는지 등 예외 처리가 필요하지만 여기선 안전하다고 가정)
	
	// 유저에게 1000 골드가 웹 상점에서 결제되어 배달되었다고 가정
	player->AddGold(1000); 

	Protocol::S_STATUS_CHANGE statusPkt;
	statusPkt.set_objectid(player->GetObjectId());
	statusPkt.set_gold(player->GetGold()); // 갱신된 골드
	SendBufferRef statusBuf = ServerPacketHandler::MakeSendBuffer(statusPkt);
	player->Send(statusBuf);

	std::cout << "Player " << playerId << " Mailbox items received from Web Shop!" << std::endl;
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
