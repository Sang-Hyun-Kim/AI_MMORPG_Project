#pragma once
#include "GameObject.h"

class GameSession;
using GameSessionRef = std::shared_ptr<GameSession>;
class SendBuffer;
using SendBufferRef = std::shared_ptr<SendBuffer>;

class Player : public GameObject
{
public:
	Player();
	virtual ~Player();

public:
	GameSessionRef GetSession() const { return _session; }
	void SetSession(GameSessionRef session) { _session = session; }

	void Send(SendBufferRef sendBuffer);

public:
	Protocol::PlayerInfo* GetPlayerInfo() { return &_playerInfo; }

private:
	GameSessionRef _session;
	Protocol::PlayerInfo _playerInfo;
};

using PlayerRef = std::shared_ptr<Player>;
