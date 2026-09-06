#include "GameSession.h"
#include "GameRoomManager.h"
#include "ServerPacketHandler.h"

GameSession::GameSession() {}

GameSession::~GameSession() {}

void GameSession::OnConnected() {
  std::wcout << L"GameSession Connected" << std::endl;
  // OnConnected에서는 방 입장(Enter)을 하지 않음! C_ENTER_GAME 패킷 수신 시 정식 입장!
}

JobTask GameSession::LoadPlayerTask(uint64 playerId) {
  auto session = static_pointer_cast<GameSession>(shared_from_this());
  std::shared_ptr<PlayerSaveData> loadedData =
      std::make_shared<PlayerSaveData>();
  loadedData->playerId = playerId;

  /*
   * [2026-09-04] mock → 실제 DB 조회 (결함 F7 2번째 겹)
   *
   * 변경 전(mock)은 아래 #if 0 블록에 원형 그대로 보존해 두었습니다.
   * mock이 있었던 이유는 게으름이 아니라 **DBConnection에 결과 셋을 읽는 API가
   * 없었기 때문**입니다(Execute는 bool만 반환). 즉 SELECT를 쓸 수단 자체가
   * 없었고, 그래서 "코루틴 → DB 워커 → 재개" 왕복이 도는지만 증명하는 형태로
   * 남아 있었습니다. 그 구조는 실제로 정상 동작했으며 값만 가짜였습니다.
   * 이번에 DBResult/ExecuteQuery를 추가하면서(C2) 비로소 실제 조회가 가능해졌습니다.
   *
   * ⚠️ conn->Execute()에 SELECT를 넘기면 결과 셋이 커넥션에 남아 다른 세션의
   *    쿼리가 오류 2014로 실패합니다. SELECT는 반드시 ExecuteQuery를 쓰십시오. [F8]
   */
  auto dbJob = [playerId, loadedData](DBConnection *conn) {
    // playerId는 Handle_C_LOGIN에서 숫자 파싱을 통과한 값이므로 SQL 문자열에
    // 그대로 넣어도 안전합니다. (정도는 Prepared Statement이며 데모 후 과제)
    const std::string selectQuery =
        "SELECT Name, Level, Gold, PosX, PosY, PosZ FROM Player WHERE PlayerId = " +
        std::to_string(playerId);

    DBResult result = conn->ExecuteQuery(selectQuery);
    if (result.HasRow()) {
      MYSQL_ROW row = result.FetchRow();
      // MYSQL_ROW의 각 칸은 NULL일 수 있으므로 반드시 널 검사 후 변환합니다.
      loadedData->name = row[0] ? row[0] : ("Player_" + std::to_string(playerId));
      loadedData->level = row[1] ? std::atoi(row[1]) : 1;
      loadedData->gold = row[2] ? std::atoi(row[2]) : 0;
      loadedData->x = row[3] ? static_cast<float>(std::atof(row[3])) : 0.f;
      loadedData->y = row[4] ? static_cast<float>(std::atof(row[4])) : 0.f;
      loadedData->z = row[5] ? static_cast<float>(std::atof(row[5])) : 0.f;

      std::cout << "[GameSession] Player Loaded from DB. PlayerId=" << playerId
                << " Name=" << loadedData->name << " Gold=" << loadedData->gold
                << " Pos=(" << loadedData->x << ", " << loadedData->y << ", "
                << loadedData->z << ")" << std::endl;
    } else {
      /*
       * 신규 캐릭터: 해당 PlayerId의 행이 아직 없습니다.
       * C# 백엔드를 거쳐 들어온 경우에는 로그인 시점에 이미 행이 만들어지므로
       * 이 경로는 검증 도구(DummyClient)가 발급한 티켓으로 들어왔을 때 주로 쓰입니다.
       * 티켓 발급에는 Redis 접근 권한이 필요하므로 외부에서 임의로 탈 수 없습니다.
       * 기본값으로 행을 만들고 그 PlayerId로 입장시킵니다.
       */
      loadedData->name = "Player_" + std::to_string(playerId);
      loadedData->level = 1;
      loadedData->gold = 100; // 초기 지급 골드
      loadedData->x = 0.f;
      loadedData->y = 0.f;
      loadedData->z = 0.f;

      const std::string insertQuery =
          "INSERT INTO Player (PlayerId, AccountId, Name, Level, Gold, PosX, PosY, PosZ) "
          "VALUES (" + std::to_string(playerId) + ", 0, '" +
          conn->EscapeString(loadedData->name) + "', 1, 100, 0, 0, 0)";

      if (conn->Execute(insertQuery)) {
        std::cout << "[GameSession] New Player Created. PlayerId=" << playerId
                  << " Name=" << loadedData->name << std::endl;
      } else {
        std::cout << "[GameSession] WARNING: New Player INSERT failed. PlayerId="
                  << playerId << " (진행은 계속하되 저장이 안 될 수 있습니다)" << std::endl;
      }
    }
  };

#if 0
  /*
   * [보존] 2026-09-04 이전의 mock 구현 — 삭제하지 않고 남깁니다.
   *
   * 남겨두는 이유:
   *   1) 데모 당일 MySQL 컨테이너가 죽었을 때 #if 1 한 글자로 접속·이동 데모를
   *      살릴 수 있는 폴백입니다. (단, 이 경로에서는 왕복 증명이 성립하지 않습니다)
   *   2) 프로젝트 규칙상 기존 코드의 임의 삭제를 금지합니다.
   *   3) "mock에서 실제 쿼리로 넘어가는 지점"이 코드에 남아 복습 자산이 됩니다.
   */
  auto dbJob = [playerId, loadedData](DBConnection *conn) {
    // Mock: 실제로는 SELECT 쿼리를 날려 값을 가져옵니다.
    // ⚠️ 아래 주석을 그대로 해제하지 마십시오. Execute()는 결과 셋을 소비하지
    //    않아 커넥션이 오염되고, 무관한 세션이 오류 2014로 실패합니다. [F8]
    //    SELECT가 필요하면 conn->ExecuteQuery()를 쓰십시오.
    // std::string query = "SELECT Gold, PosX, PosY, PosZ FROM Player WHERE
    // PlayerId = " + std::to_string(playerId); conn->Execute(query);

    // 임시 데이터 세팅
    loadedData->gold = 100; // 초기 지급 골드
    loadedData->x = 0;
    loadedData->y = 0;
    loadedData->z = 0;
    loadedData->name = "Player_" + std::to_string(playerId);
  };
#endif

  // null 큐를 넘겨 DB 스레드에서 코루틴을 직접 재개시킵니다.
  co_await DBAwaitable(dbJob, nullptr);

  PlayerRef player = std::make_shared<Player>();
  // [O1] 상태 단일화: ID/이름/좌표는 GameObject::_info 한 곳에만 기록합니다.
  // 과거에는 _info와 _playerInfo 두 사본에 나눠 기록해서,
  // S_SPAWN(=_info)에는 이름이 비고 좌표가 원점이었고,
  // GetSaveData()(=_playerInfo)는 이동 결과를 보지 못해 DB에 (0,0,0)만 저장했습니다.
  player->SetObjectId(playerId);
  player->GetObjectInfo()->set_name(loadedData->name);
  player->GetPosInfo()->set_x(loadedData->x);
  player->GetPosInfo()->set_y(loadedData->y);
  player->GetPosInfo()->set_z(loadedData->z);
  player->SetGold(loadedData->gold);
  // [F9] 캐릭터 레벨을 DB 값으로 채웁니다.
  // 과거에는 어디에서도 대입되지 않아 S_ENTER_GAME/S_SPAWN에 항상 0이 실렸습니다.
  // (여기서 Level은 캐릭터 레벨입니다. 맵/존 ID는 스키마에 아직 존재하지 않으며
  //  기획 미착수 항목으로 AI_HANDOFF.md에 기록되어 있습니다.)
  player->SetLevel(loadedData->level);
  player->SetSession(session);

  session->SetPlayer(player);

  // 1. [핵심] 클라이언트에게 S_ENTER_GAME을 "가장 먼저" 전송! (MyPlayerId 세팅 확정)
  Protocol::S_ENTER_GAME enterPkt;
  enterPkt.set_success(true);
  enterPkt.mutable_player()->CopyFrom(player->MakePlayerInfo());
  session->Send(ServerPacketHandler::MakeSendBuffer(enterPkt));

  std::cout << "==================================================" << std::endl;
  std::cout << "[Server] S_ENTER_GAME Sent! PlayerId: " << playerId << std::endl;
  std::cout << "==================================================" << std::endl;

  // 2. [핵심] 그 후 GameRoom에 입장시키며 S_SPAWN 브로드캐스트!
  GameRoomRef room = GGameRoomManager->GetRoom(1);
  if (room != nullptr) {
    room->DoAsync(&GameRoom::Enter,
                  std::static_pointer_cast<GameObject>(player));
  }
}

void GameSession::OnDisconnected() {
  std::wcout << L"GameSession Disconnected" << std::endl;

  if (_player) {
    GameRoomRef room = _player->GetRoom();
    if (room != nullptr) {
      room->DoAsync(&GameRoom::Leave,
                    std::static_pointer_cast<GameObject>(_player));
    }
    _player->SetSession(nullptr);
    _player = nullptr;
  }
}

void GameSession::OnRecvPacket(std::span<std::byte> buffer) {
  PacketSessionRef session = GetPacketSessionRef();
  ServerPacketHandler::HandlePacket(session, buffer);
}

void GameSession::OnSend(int32 len) {
  // std::wcout << L"OnSend: " << len << L" bytes" << std::endl;
}
