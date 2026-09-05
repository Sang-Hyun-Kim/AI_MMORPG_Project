#include "GameRoom.h"
#include "CoreGlobal.h"
#include "DBAwaitable.h"
#include "DBConnectionPool.h"
#include "JobTimer.h"
#include "RedisManager.h"
#include "ServerPacketHandler.h"
#include <cmath>    // [AOI-1] std::floor — 음수 좌표 격자 계산
#include <sstream>  // [AOI-1] 통계 한 줄 조립 (cout 인터리빙 방지)
#include <sw/redis++/redis++.h>

GameRoom::GameRoom() {}

GameRoom::~GameRoom() {}

void GameRoom::Init() {
  // 최초 틱 예약: 50ms 후 첫 Update 실행
  DoTimer(50, &GameRoom::Update);

  // 오토 세이브 예약: 1분(60000ms) 후 실행 (테스트용)
  DoTimer(60000, &GameRoom::AutoSave);
}

void GameRoom::Update() {
  // 50ms마다 틱 실행 (JobTimer 기반 예약)
  // TODO: 몬스터 AI 틱 처리 및 패킷 Flush 처리

  // 다음 틱을 50ms 후로 예약 (스레드를 즉시 반납)
  // GJobTimer의 Distribute 주기(현재 100ms)에 따라 실제 실행은 50~150ms 범위
  DoTimer(50, &GameRoom::Update);
}

void GameRoom::AutoSave() {
  for (auto &pair : _players) {
    PlayerSaveData snapshot = pair.second->GetSaveData();
    DoAsync(&GameRoom::SavePlayerToDB, snapshot);
  }

  std::cout << "[GameRoom] AutoSave Triggered for " << _players.size()
            << " players." << std::endl;

  // [AOI-1] 60초마다 AOI 관측 통계를 함께 출력합니다.
  // 새 타이머를 만들지 않고 기존 틱에 얹었습니다.
  PrintAoiStats();

  DoTimer(60000, &GameRoom::AutoSave);
}

void GameRoom::Enter(GameObjectRef gameObject) {
  if (gameObject == nullptr)
    return;

  PlayerRef player = std::static_pointer_cast<Player>(gameObject);
  uint64 objectId = player->GetObjectId();

  _players[objectId] = player;
  player->SetRoom(static_pointer_cast<GameRoom>(shared_from_this()));

  // [AOI-1] 격자에 등록. _players 갱신과 반드시 짝을 이뤄야 합니다.
  AddToSector(objectId, player->GetPosInfo()->x(), player->GetPosInfo()->y());

  // S_SPAWN 브로드캐스트
  Protocol::S_SPAWN spawnPkt;
  Protocol::ObjectInfo *info = spawnPkt.add_objects();
  info->CopyFrom(*player->GetObjectInfo());

  SendBufferRef sendBuffer = ServerPacketHandler::MakeSendBuffer(spawnPkt);

  /*
   * [V19 / 2026-09-04 5차] 입·퇴장 브로드캐스트를 대칭으로 맞췄습니다.
   *
   *   이전: Enter 는 BroadcastToAdjacentSectors(AOI 반경 2000), Leave 는 Broadcast(방 전체).
   *   비대칭의 결과로 두 플레이어가 2000 이상 떨어져 있으면 서로를 영영 보지 못하고,
   *   반대로 받은 적 없는 오브젝트의 S_DESPAWN 을 받을 수 있었습니다.
   *   바로 아래 "본인에게 방 전체 목록 전송"도 이미 AOI 를 적용하지 않으므로,
   *   Leave 쪽 기준(방 전체)에 맞추는 것이 세 경로 모두와 일관됩니다.
   *
   *   ⚠️ AOI 반경(2000) 자체는 손대지 않았습니다 — 사용자 지시(2026-09-04).
   *      AOI 는 진입/이탈 추적을 포함해 입·퇴장을 함께 설계할 때 제대로 넣습니다.
   *      그때까지 S_MOVE(HandleMove) 와 S_ATTACK(HandleAttack) 은 의도적으로
   *      BroadcastToAdjacentSectors 로 남겨 둡니다. 여기만 보고 나머지를 따라
   *      바꾸지 마십시오. 반경 개념이 통째로 무력화됩니다.
   */
  Broadcast(sendBuffer);

  // 본인에게는 현재 방에 있는 유저들 정보를 전송
  Protocol::S_SPAWN mySpawnPkt;
  for (auto &pair : _players) {
    if (pair.first != objectId) {
      Protocol::ObjectInfo *pInfo = mySpawnPkt.add_objects();
      pInfo->CopyFrom(*pair.second->GetObjectInfo());
    }
  }

  if (mySpawnPkt.objects_size() > 0) {
    SendBufferRef mySpawnBuffer =
        ServerPacketHandler::MakeSendBuffer(mySpawnPkt);
    player->Send(mySpawnBuffer);
  }
}

void GameRoom::Leave(GameObjectRef gameObject) {
  if (gameObject == nullptr)
    return;

  PlayerRef player = std::static_pointer_cast<Player>(gameObject);
  if (player) {
    // DB 스냅샷 저장 (Leave 이전에 복사해야 안전)
    PlayerSaveData snapshot = player->GetSaveData();
    DoAsync(&GameRoom::SavePlayerToDB, snapshot);
  }

  uint64 objectId = gameObject->GetObjectId();

  // [AOI-1] 격자에서 먼저 제거합니다. _players 에서 지운 뒤에는 위치를 읽어도
  // 되지만, 순서를 고정해 두는 편이 다음 사람이 실수할 여지가 적습니다.
  RemoveFromSector(objectId, gameObject->GetPosInfo()->x(),
                   gameObject->GetPosInfo()->y());

  if (_players.erase(objectId) == 0)
    return;

  gameObject->SetRoom(nullptr);

  Protocol::S_DESPAWN despawnPkt;
  despawnPkt.add_objectids(objectId);

  SendBufferRef sendBuffer = ServerPacketHandler::MakeSendBuffer(despawnPkt);
  // [V19 / 2026-09-04 5차] Enter 와 대칭. 이전 주석의 "임시로"는 Enter 가 AOI 였던 시절의
  // 표현이었습니다. 이제 입·퇴장 모두 방 전체가 기준이며 이것이 의도된 동작입니다.
  Broadcast(sendBuffer);
}

JobTask GameRoom::SavePlayerToDB(PlayerSaveData data) {
  auto jobQueue = shared_from_this();

  /*
   * [2026-09-04] UPDATE → UPSERT 전환 (결함 F7 3번째 겹)
   *
   * 변경 전:
   *     UPDATE Player SET Gold = .., PosX = .. WHERE PlayerId = <id>;
   *     if (conn->Execute(query)) { }
   *
   * 문제:
   *   playerId가 프로세스 카운터(1, 2, 3…)였는데 DB에는 mailbox.sql이 넣은
   *   PlayerId=0인 DummyPlayer 한 행뿐이라 **일치하는 행이 없었습니다.**
   *   MySQL은 0행을 갱신해도 쿼리를 성공으로 처리하므로 Execute()는 true를
   *   반환했고, 아래 "DB Save Complete" 로그가 정상 출력되었습니다.
   *   즉 **로그는 저장됐다고 말했지만 DB는 한 번도 바뀌지 않았습니다.**
   *   이것이 "이동해도 매번 같은 자리에서 시작"의 원인 중 하나입니다.
   *
   * 조치:
   *   1) INSERT ... ON DUPLICATE KEY UPDATE로 바꿔, 행이 없으면 만들고 있으면 갱신.
   *   2) GetAffectedRows()로 실제 반영 여부를 확인해 로그가 거짓말하지 않게 함.
   *      (MySQL의 affected_rows는 INSERT=1, UPDATE=2, 변경 없음=0을 반환합니다.
   *       0이어도 "값이 이미 같아서 갱신할 게 없었다"는 정상 케이스일 수 있으므로
   *       실패로 단정하지 않고 구분해 로깅합니다.)
   *   3) [F9] Level을 저장 대상에 포함.
   */
  auto dbJob = [data](DBConnection *conn) {
    const std::string query =
        "INSERT INTO Player (PlayerId, Name, Level, Gold, PosX, PosY, PosZ) VALUES (" +
        std::to_string(data.playerId) + ", '" + conn->EscapeString(data.name) + "', " +
        std::to_string(data.level) + ", " + std::to_string(data.gold) + ", " +
        std::to_string(data.x) + ", " + std::to_string(data.y) + ", " +
        std::to_string(data.z) + ") " +
        "ON DUPLICATE KEY UPDATE "
        "Level = VALUES(Level), Gold = VALUES(Gold), "
        "PosX = VALUES(PosX), PosY = VALUES(PosY), PosZ = VALUES(PosZ)";

    if (conn->Execute(query) == false) {
      std::cout << "[GameRoom] DB Save FAILED. PlayerId=" << data.playerId << std::endl;
      return;
    }

    const uint64 affected = conn->GetAffectedRows();
    if (affected == 0) {
      // 쿼리는 성공했으나 반영된 행이 없음 = 저장할 변경이 없었음.
      std::cout << "[GameRoom] DB Save: no change. PlayerId=" << data.playerId << std::endl;
    }
  };

  co_await DBAwaitable(dbJob, jobQueue);

  std::cout << "Player " << data.name << " (PlayerId=" << data.playerId
            << ") DB Save Complete. Pos=(" << data.x << ", " << data.y << ", "
            << data.z << ") Gold=" << data.gold << std::endl;
}

void GameRoom::HandleMove(PlayerRef player, Protocol::C_MOVE pkt) {
  if (player == nullptr)
    return;

  /*
   * [AOI-1] 격자 갱신은 좌표를 덮어쓰기 **전에** 옛 좌표를 떠 놔야 합니다.
   *   CopyFrom 이후에는 이전 셀을 알 수 없어 격자에 유령 엔트리가 남습니다.
   *   같은 셀 안에서의 이동이면 MoveSector 가 알아서 아무것도 하지 않습니다.
   */
  const float oldX = player->GetPosInfo()->x();
  const float oldY = player->GetPosInfo()->y();

  // 위치 갱신 (서버 검증 추가 가능)
  player->GetPosInfo()->CopyFrom(pkt.posinfo());

  MoveSector(player->GetObjectId(), oldX, oldY, player->GetPosInfo()->x(),
             player->GetPosInfo()->y());

  // 주변 브로드캐스팅
  Protocol::S_MOVE movePkt;
  movePkt.set_objectid(player->GetObjectId());
  movePkt.mutable_posinfo()->CopyFrom(pkt.posinfo());

  SendBufferRef sendBuffer = ServerPacketHandler::MakeSendBuffer(movePkt);
  BroadcastToAdjacentSectors(player->GetPosInfo()->x(),
                             player->GetPosInfo()->y(), sendBuffer);
}

/*
 * HandleAttack (고블린 사냥 시나리오)
 * 기능: 유저가 몬스터(또는 허수아비)를 공격했을 때 처리하는 핸들러.
 * 데이터 흐름 (Data Flow):
 * 1. 클라이언트가 C_ATTACK 패킷 전송 (타겟 ID 포함)
 * 2. 서버가 타겟 사망 여부 판정 (데모에서는 무조건 사망한다고 가정)
 * 3. 클라이언트들에게 S_ATTACK 패킷 브로드캐스트 (타격 모션 재생용)
 * 4. 사망 시, 해당 유저에게 골드(+100) 지급 후 S_STATUS_CHANGE 브로드캐스트
 * 5. [중요] 획득된 골드 로그를 DB가 아닌 Redis Stream(gold_log)에 Publish
 * (비동기 빠른 처리)
 */
void GameRoom::HandleAttack(PlayerRef player, Protocol::C_ATTACK pkt) {
  if (player == nullptr)
    return;

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
  BroadcastToAdjacentSectors(player->GetPosInfo()->x(),
                             player->GetPosInfo()->y(), attackBuf);

  // 2. 골드 획득 처리 및 통보
  if (isKilled) {
    // Player 객체에 임시로 골드를 저장 (Player.h에 gold 멤버가 있다고 가정)
    player->AddGold(dropGold);

    Protocol::S_STATUS_CHANGE statusPkt;
    statusPkt.set_objectid(player->GetObjectId());
    statusPkt.set_gold(player->GetGold());
    SendBufferRef statusBuf = ServerPacketHandler::MakeSendBuffer(statusPkt);

    // 본인(혹은 시야 내 인원)에게 갱신된 골드 전송
    player->Send(statusBuf);

    // 3. Redis Stream에 골드 획득 로그 발행 (Fire and Forget)
    if (GRedisManager && GRedisManager->GetRedis()) {
      try {
        std::unordered_map<std::string, std::string> logData = {
            {"player_id", std::to_string(player->GetObjectId())},
            {"amount", std::to_string(dropGold)},
            {"reason", "goblin_hunt"}};
        // xadd를 사용하여 stream:gold_log 스트림에 로그 추가 (비동기 큐잉 역할)
        GRedisManager->GetRedis()->xadd("stream:gold_log", "*", logData.begin(),
                                        logData.end());
      } catch (const sw::redis::Error &e) {
        std::cout << "Redis XADD Error: " << e.what() << std::endl;
      }
    }
  }
}

/*
 * HandleCheckMailbox (웹 상점 연동 시나리오)
 * 기능: 웹 상점에서 결제 후, 인게임에서 우편함 수령을 요청했을 때의 처리
 * 특징: 직접 DB 쿼리가 필요하므로, 코루틴(ProcessMailboxDB)으로 비동기 작업을
 * 넘깁니다.
 */
void GameRoom::HandleCheckMailbox(PlayerRef player,
                                  Protocol::C_CHECK_MAILBOX pkt) {
  if (player == nullptr)
    return;

  // DB 조회를 시작 (비동기)
  DoAsync(&GameRoom::ProcessMailboxDB, player);
}

/*
 * ProcessMailboxDB (우편함 패턴 트랜잭션 - 비동기 DB 처리)
 * 기능: C# 서버가 결제 후 MySQL Mailbox 테이블에 넣어둔 아이템(골드)을 읽고
 * 삭제(수령)합니다. 데이터 흐름 (Data Flow):
 * 1. 게임 스레드에서 호출되면, co_await DBAwaitable을 만나 일시정지(Suspend)
 * 됨.
 * 2. 백그라운드 DB 스레드가 MySQL에 "SELECT -> 수령 -> DELETE" 트랜잭션 수행.
 * 3. 완료 후 게임 스레드로 복귀(Resume)하여, 유저에게 아이템 획득
 * 패킷(S_STATUS_CHANGE) 전송.
 *
 * *주의*: 비동기 처리 도중 유저가 접속 종료(Leave)될 수 있으므로, 재개(Resume)
 * 후 player 참조의 유효성을 체크해야 합니다. (이 예제에선 shared_ptr 캡처로
 * 생명주기 연장)
 */
JobTask GameRoom::ProcessMailboxDB(PlayerRef player) {
  auto jobQueue = shared_from_this(); // 원래 스레드로 돌아오기 위한 큐 저장
  uint64 playerId = player->GetObjectId(); // 추후 유효성 검증용

  // DB 워커 스레드에서 실행될 실제 작업 (람다 캡처로 데이터 복사)
  auto dbJob = [playerId](DBConnection *conn) {
    // 실제 상용 환경에선 트랜잭션(START TRANSACTION)과 함께
    // SELECT * FROM Mailbox WHERE PlayerId = ? FOR UPDATE -> DELETE -> COMMIT
    // 이 이루어집니다.
    // (본 데모에선 테이블 스키마가 없으므로 성공했다고 가정하고 로그만
    // 남깁니다.)
    std::string query =
        "DELETE FROM Mailbox WHERE PlayerId = " + std::to_string(playerId) +
        " LIMIT 1;";
    conn->Execute(query);
  };

  // 1. 여기서 게임 스레드는 중단되고, DB 스레드로 dbJob이 넘어갑니다.
  co_await DBAwaitable(dbJob, jobQueue);

  // 2. DB 작업 완료 후, 다시 게임 스레드로 돌아왔습니다! (Resume)
  // (비동기 대기 중에 유저가 나갔는지 등 예외 처리가 필요하지만 여기선
  // 안전하다고 가정)

  // 유저에게 1000 골드가 웹 상점에서 결제되어 배달되었다고 가정
  player->AddGold(1000);

  Protocol::S_STATUS_CHANGE statusPkt;
  statusPkt.set_objectid(player->GetObjectId());
  statusPkt.set_gold(player->GetGold()); // 갱신된 골드
  SendBufferRef statusBuf = ServerPacketHandler::MakeSendBuffer(statusPkt);
  player->Send(statusBuf);

  std::cout << "Player " << playerId << " Mailbox items received from Web Shop!"
            << std::endl;
}

void GameRoom::Broadcast(SendBufferRef sendBuffer) {
  for (auto &pair : _players) {
    pair.second->Send(sendBuffer);
  }
}

/*
 * ─── Uniform Grid AOI [AOI-1 / 2026-09-06] ──────────────────────────────────
 *
 * 이전 구현은 이름만 Sector 였고 실제로는 _players 전체를 순회했습니다.
 * 이제 좌표를 kCellSize 격자로 나눠 셀별 집합을 유지하고, 질의 시
 * 반경이 걸치는 셀만 훑습니다.
 *
 * ⚠️ 반환 결과는 이전과 동일합니다. 셀로 후보를 좁힌 뒤에도
 *    정확한 거리 검사를 그대로 수행하기 때문입니다. (가속 구조일 뿐)
 */

int32 GameRoom::SectorCoord(float v) {
  // [버그 수정] static_cast<int32> 는 0 방향 절단이라 -500 과 +500 이 모두 0 이
  // 됩니다. floor 를 써야 음수 영역에서도 격자 간격이 균일해집니다.
  return static_cast<int32>(std::floor(v / kCellSize));
}

int64 GameRoom::MakeSectorKey(int32 sx, int32 sy) {
  // 이전의 sx + sy * 1000 은 |sx| 가 500 을 넘으면 다른 셀과 값이 겹칩니다.
  // 상위 32비트에 sx, 하위 32비트에 sy 를 담아 충돌을 없앱니다.
  return (static_cast<int64>(sx) << 32) |
         static_cast<int64>(static_cast<uint32>(sy));
}

int64 GameRoom::GetSectorIndex(float x, float y) {
  return MakeSectorKey(SectorCoord(x), SectorCoord(y));
}

void GameRoom::AddToSector(uint64 objectId, float x, float y) {
  _sectors[GetSectorIndex(x, y)].insert(objectId);
}

void GameRoom::RemoveFromSector(uint64 objectId, float x, float y) {
  const int64 key = GetSectorIndex(x, y);
  auto it = _sectors.find(key);
  if (it == _sectors.end())
    return;

  it->second.erase(objectId);
  if (it->second.empty())
    _sectors.erase(it); // 빈 셀은 들고 있지 않는다 (맵이 무한히 자라는 것 방지)
}

void GameRoom::MoveSector(uint64 objectId, float oldX, float oldY, float newX,
                          float newY) {
  const int64 from = GetSectorIndex(oldX, oldY);
  const int64 to = GetSectorIndex(newX, newY);
  if (from == to)
    return; // 같은 셀 안에서의 이동은 자료구조를 건드릴 필요가 없다

  RemoveFromSector(objectId, oldX, oldY);
  _sectors[to].insert(objectId);
}

std::vector<PlayerRef> GameRoom::GetAdjacentSectorPlayers(float x, float y) {
  std::vector<PlayerRef> adjacentPlayers;

  const float AOI_RADIUS_SQ = kAoiRadius * kAoiRadius;
  const int32 cx = SectorCoord(x);
  const int32 cy = SectorCoord(y);

  // 통계: 그리드가 없었다면 _players 전체와 비교했어야 한다
  _aoiStats.queryCount += 1;
  _aoiStats.naiveCompareCount += _players.size();

  for (int32 sx = cx - kCellSpan; sx <= cx + kCellSpan; sx++) {
    for (int32 sy = cy - kCellSpan; sy <= cy + kCellSpan; sy++) {
      _aoiStats.cellVisitCount += 1;

      auto it = _sectors.find(MakeSectorKey(sx, sy));
      if (it == _sectors.end())
        continue; // 빈 셀은 즉시 건너뛴다 — 여기서 비용이 줄어든다

      for (uint64 objectId : it->second) {
        auto pit = _players.find(objectId);
        if (pit == _players.end())
          continue; // 셀과 _players 가 어긋난 경우 (방어)

        const PlayerRef &player = pit->second;
        const float dx = player->GetPosInfo()->x() - x;
        const float dy = player->GetPosInfo()->y() - y;

        _aoiStats.compareCount += 1; // 실제 거리 비교 1회

        if (dx * dx + dy * dy <= AOI_RADIUS_SQ)
          adjacentPlayers.push_back(player);
      }
    }
  }

  return adjacentPlayers;
}

void GameRoom::PrintAoiStats() const {
  if (_aoiStats.queryCount == 0)
    return;

  const double avgGrid =
      static_cast<double>(_aoiStats.compareCount) / _aoiStats.queryCount;
  const double avgNaive =
      static_cast<double>(_aoiStats.naiveCompareCount) / _aoiStats.queryCount;

  std::ostringstream oss; // 한 줄로 조립해 출력 (멀티스레드 인터리빙 방지)
  oss << "[AOI] queries=" << _aoiStats.queryCount
      << "  compares(grid)=" << _aoiStats.compareCount
      << "  compares(naive)=" << _aoiStats.naiveCompareCount
      << "  avg/query: " << avgGrid << " vs " << avgNaive;
  if (avgGrid > 0.0)
    oss << "  (x" << (avgNaive / avgGrid) << " 감소)";
  oss << "  cells=" << _aoiStats.cellVisitCount
      << "  occupied=" << _sectors.size() << "\n";

  // 본문은 한 번의 << 로 내보내 인터리빙을 막고, flush 는 따로 겁니다.
  // flush 가 없으면 stdout 이 파일/파이프로 리다이렉트됐을 때 블록 버퍼링에
  // 걸려 로그가 보이지 않습니다 (2026-08-10 로그의 stdout 소실과 같은 원인).
  std::cout << oss.str() << std::flush;
}

void GameRoom::BroadcastToAdjacentSectors(float x, float y,
                                          SendBufferRef sendBuffer) {
  std::vector<PlayerRef> players = GetAdjacentSectorPlayers(x, y);
  for (PlayerRef player : players) {
    player->Send(sendBuffer);
  }
}
