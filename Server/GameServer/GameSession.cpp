#include "GameSession.h"
#include "GameRoomManager.h"
#include "ServerPacketHandler.h"

GameSession::GameSession() {}

GameSession::~GameSession() {}

void GameSession::OnConnected() {
  std::wcout << L"GameSession Connected" << std::endl;

  // 더미 접속 시 임시 ID 발급 (실제로는 로그인 패킷에서 계정 ID를 받아서
  // 처리해야 함)
  static std::atomic<uint64> idGenerator = 1;
  uint64 tempPlayerId = idGenerator.fetch_add(1);

  // DB에서 데이터를 비동기로 로드하고 방에 입장시키는 코루틴 호출
  LoadPlayerTask(1, tempPlayerId);
}

JobTask GameSession::LoadPlayerTask(uint64 accountId, uint64 playerId) {
  auto session = static_pointer_cast<GameSession>(shared_from_this());
  std::shared_ptr<PlayerSaveData> loadedData =
      std::make_shared<PlayerSaveData>();
  loadedData->playerId = playerId;

  auto dbJob = [playerId, loadedData](DBConnection *conn) {
    // Mock: 실제로는 SELECT 쿼리를 날려 값을 가져옵니다.
    // std::string query = "SELECT Gold, PosX, PosY, PosZ FROM Player WHERE
    // PlayerId = " + std::to_string(playerId); conn->Execute(query);

    // 임시 데이터 세팅
    loadedData->gold = 100; // 초기 지급 골드
    loadedData->x = 0;
    loadedData->y = 0;
    loadedData->z = 0;
    loadedData->name = "Player_" + std::to_string(playerId);
  };

  // null 큐를 넘겨 DB 스레드에서 코루틴을 직접 재개시킵니다.
  co_await DBAwaitable(dbJob, nullptr);

  PlayerRef player = std::make_shared<Player>();
  player->SetObjectId(playerId);
  player->GetPlayerInfo()->mutable_objectinfo()->set_name(loadedData->name);
  player->GetPlayerInfo()->mutable_objectinfo()->mutable_posinfo()->set_x(loadedData->x);
  player->GetPlayerInfo()->mutable_objectinfo()->mutable_posinfo()->set_y(loadedData->y);
  player->GetPlayerInfo()->mutable_objectinfo()->mutable_posinfo()->set_z(loadedData->z);
  player->SetGold(loadedData->gold);
  player->SetSession(session);

  session->SetPlayer(player);

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
