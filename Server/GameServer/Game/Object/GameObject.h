#pragma once
#include "CorePch.h"
#include "Protocol.pb.h"

class GameRoom;
using GameRoomRef = std::shared_ptr<GameRoom>;

class GameObject : public std::enable_shared_from_this<GameObject>
{
public:
	GameObject();
	virtual ~GameObject();

public:
	Protocol::ObjectInfo* GetObjectInfo() { return &_info; }
	uint64 GetObjectId() const { return _info.objectid(); }
	void SetObjectId(uint64 id) { _info.set_objectid(id); }

	Protocol::PositionInfo* GetPosInfo() { return _info.mutable_posinfo(); }

	GameRoomRef GetRoom() const { return _room.lock(); }
	void SetRoom(GameRoomRef room) { _room = room; }

protected:
	Protocol::ObjectInfo _info;
	std::weak_ptr<GameRoom> _room;
};

using GameObjectRef = std::shared_ptr<GameObject>;
