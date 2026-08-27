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

	void Enter(GameObjectRef gameObject);
	void Leave(GameObjectRef gameObject);

	void HandleMove(PlayerRef player, Protocol::C_MOVE pkt);

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
