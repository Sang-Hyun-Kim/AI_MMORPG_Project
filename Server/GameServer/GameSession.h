#pragma once
#include "Session.h"
#include "Player.h"

class GameSession : public PacketSession
{
public:
	GameSession();
	virtual ~GameSession();

	virtual void OnConnected() override;
	virtual void OnDisconnected() override;
	virtual void OnRecvPacket(std::span<std::byte> buffer) override;
	virtual void OnSend(int32 len) override;

public:
	PlayerRef GetPlayer() { return _player; }
	void SetPlayer(PlayerRef player) { _player = player; }

private:
	PlayerRef _player;
};
