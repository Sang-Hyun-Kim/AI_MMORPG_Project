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
