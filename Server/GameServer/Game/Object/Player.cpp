#include "Player.h"
#include "GameSession.h"

Player::Player()
{
}

Player::~Player()
{
}

void Player::Send(SendBufferRef sendBuffer)
{
	if (_session)
	{
		_session->Send(sendBuffer);
	}
}

PlayerSaveData Player::GetSaveData()
{
	PlayerSaveData data;
	data.playerId = _playerInfo.objectid();
	data.name = _playerInfo.name();
	data.x = _playerInfo.posinfo().x();
	data.y = _playerInfo.posinfo().y();
	data.z = _playerInfo.posinfo().z();
	data.gold = _gold;
	return data;
}
