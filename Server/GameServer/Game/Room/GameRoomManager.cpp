#include "GameRoomManager.h"

void GameRoomManager::Init()
{
	// 기본 로비 방 생성
	CreateRoom();
}

GameRoomRef GameRoomManager::CreateRoom()
{
	std::lock_guard<std::mutex> lock(_lock);
	
	GameRoomRef room = std::make_shared<GameRoom>();
	room->Init();
	
	_rooms[_roomId++] = room;
	return room;
}

bool GameRoomManager::RemoveRoom(uint32 roomId)
{
	std::lock_guard<std::mutex> lock(_lock);
	return _rooms.erase(roomId) > 0;
}

GameRoomRef GameRoomManager::GetRoom(uint32 roomId)
{
	std::lock_guard<std::mutex> lock(_lock);
	auto it = _rooms.find(roomId);
	if (it != _rooms.end())
		return it->second;
	return nullptr;
}

void GameRoomManager::BroadcastAutoSave()
{
	std::lock_guard<std::mutex> lock(_lock);
	for (auto& pair : _rooms)
	{
		// 각 방의 AutoSave(전체 유저 DB 저장)를 비동기로 예약합니다.
		pair.second->DoAsync(&GameRoom::AutoSave);
	}
}
