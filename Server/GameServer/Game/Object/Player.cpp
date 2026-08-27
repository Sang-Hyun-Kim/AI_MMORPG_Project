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
