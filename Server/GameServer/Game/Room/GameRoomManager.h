#pragma once
#include "GameRoom.h"

class GameRoomManager
{
public:
	static GameRoomManager* Instance()
	{
		static GameRoomManager instance;
		return &instance;
	}

	void Init();
	GameRoomRef CreateRoom();
	bool RemoveRoom(uint32 roomId);
	GameRoomRef GetRoom(uint32 roomId);

	// Phase 3: Graceful Shutdown 시 모든 방의 유저 정보를 저장하기 위함
	void BroadcastAutoSave();

private:
	std::mutex _lock;
	std::unordered_map<uint32, GameRoomRef> _rooms;
	uint32 _roomId = 1;
};

#define GGameRoomManager GameRoomManager::Instance()
