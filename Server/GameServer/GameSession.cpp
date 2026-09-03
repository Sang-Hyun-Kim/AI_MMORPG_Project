#include "GameSession.h"
#include "GameRoomManager.h"
#include "ServerPacketHandler.h"

GameSession::GameSession() {}

GameSession::~GameSession() {}

void GameSession::OnConnected() {
  std::wcout << L"GameSession Connected" << std::endl;
  // OnConnected에서는 방 입장(Enter)을 하지 않음! C_ENTER_GAME 패킷 수신 시 정식 입장!
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
  player->GetPlayerInfo()->mutable_objectinfo()->set_objectid(playerId);
  player->GetPlayerInfo()->mutable_objectinfo()->set_name(loadedData->name);
  player->GetPlayerInfo()->mutable_objectinfo()->mutable_posinfo()->set_x(loadedData->x);
  player->GetPlayerInfo()->mutable_objectinfo()->mutable_posinfo()->set_y(loadedData->y);
  player->GetPlayerInfo()->mutable_objectinfo()->mutable_posinfo()->set_z(loadedData->z);
  player->SetGold(loadedData->gold);
  player->SetSession(session);

  session->SetPlayer(player);

  // 1. [핵심] 클라이언트에게 S_ENTER_GAME을 "가장 먼저" 전송! (MyPlayerId 세팅 확정)
  Protocol::S_ENTER_GAME enterPkt;
  enterPkt.set_success(true);
  enterPkt.mutable_player()->CopyFrom(*player->GetPlayerInfo());
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
