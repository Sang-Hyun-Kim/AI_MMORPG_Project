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

	JobTask LoadPlayerTask(uint64 accountId, uint64 playerId);

public:
	PlayerRef GetPlayer() { return _player; }
	void SetPlayer(PlayerRef player) { _player = player; }

private:
	PlayerRef _player;
};
