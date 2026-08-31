#pragma once
#include "JobQueue.h"
#include "Player.h"

class GameRoom : public JobQueue
{
public:
	GameRoom();
	virtual ~GameRoom();

	void Init();
	void Update();
	void AutoSave();

	void Enter(GameObjectRef gameObject);
	void Leave(GameObjectRef gameObject);

	JobTask SavePlayerToDB(PlayerSaveData data);

	void HandleMove(PlayerRef player, Protocol::C_MOVE pkt);
	
	// Phase 3: 고블린 사냥 및 상점 관련 핸들러
	void HandleAttack(PlayerRef player, Protocol::C_ATTACK pkt);
	void HandleCheckMailbox(PlayerRef player, Protocol::C_CHECK_MAILBOX pkt);
	JobTask ProcessMailboxDB(PlayerRef player); // DB에서 우편함 비동기 조회 및 수령

	void Broadcast(SendBufferRef sendBuffer);

private:
	// Sector-based AOI helper
	// sector size: 1000.0f
	int32 GetSectorIndex(float x, float y);
	std::vector<PlayerRef> GetAdjacentSectorPlayers(float x, float y);
	void BroadcastToAdjacentSectors(float x, float y, SendBufferRef sendBuffer);

private:
	std::unordered_map<uint64, PlayerRef> _players;
	// _monsters
};

using GameRoomRef = std::shared_ptr<GameRoom>;
