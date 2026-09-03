#include "Player.h"
#include "GameSession.h"

Player::Player() {}

Player::~Player() {}

void Player::Send(SendBufferRef sendBuffer) {
  if (_session) {
    _session->Send(sendBuffer);
  }
}

/*
 * MakePlayerInfo
 * GameObject::_info(단일 소유 상태)로부터 전송용 PlayerInfo를 조립합니다.
 * [O1] 참조: Player.h의 선언부 주석 참고.
 */
Protocol::PlayerInfo Player::MakePlayerInfo() {
  Protocol::PlayerInfo info;
  info.mutable_objectinfo()->CopyFrom(_info);
  info.set_level(_level);
  return info;
}

/*
 * GetSaveData
 * [O1] 이제 _info(= HandleMove가 실제로 갱신하는 그 사본)에서 읽습니다.
 * 과거에는 _playerInfo에서 읽어, 이동한 좌표가 DB에 전혀 반영되지 않았습니다.
 */
PlayerSaveData Player::GetSaveData() {
  PlayerSaveData data;
  data.playerId = _info.objectid();
  data.name = _info.name();
  data.x = _info.posinfo().x();
  data.y = _info.posinfo().y();
  data.z = _info.posinfo().z();
  data.gold = _gold;
  return data;
}
