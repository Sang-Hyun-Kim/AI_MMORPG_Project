#include "Player.h"
#include "GameSession.h"

Player::Player() {}

Player::~Player() {}

void Player::Send(SendBufferRef sendBuffer) {
  if (_session) {
    _session->Send(sendBuffer);
  }
}

PlayerSaveData Player::GetSaveData() {
  PlayerSaveData data;
  data.playerId = _playerInfo.objectinfo().objectid();
  data.name = _playerInfo.objectinfo().name();
  data.x = _playerInfo.objectinfo().posinfo().x();
  data.y = _playerInfo.objectinfo().posinfo().y();
  data.z = _playerInfo.objectinfo().posinfo().z();
  data.gold = _gold;
  return data;
}
