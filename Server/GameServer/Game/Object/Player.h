#pragma once
#include "GameObject.h"

class GameSession;
using GameSessionRef = std::shared_ptr<GameSession>;
class SendBuffer;
using SendBufferRef = std::shared_ptr<SendBuffer>;

/*
 * PlayerSaveData
 * 역할: 플레이어의 현재 상태를 스냅샷(값 복사) 형태로 들고 있는 구조체입니다.
 * 이유:
 *   비동기 DB 저장 시, 람다나 코루틴으로 참조(Player*)를 넘기면
 *   DB 스레드가 동작할 때 이미 Player 객체가 삭제되었거나 데이터가 변경되어
 *   스레드 경합(Data Race)이 발생할 수 있습니다. 
 *   따라서 안전하게 값만 복사해서 DB 쿼리에 활용합니다.
 */
struct PlayerSaveData
{
	uint64 playerId = 0;
	std::string name;
	float x = 0;
	float y = 0;
	float z = 0;
	int32 gold = 0;
};

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
	PlayerSaveData GetSaveData();

	int32 GetGold() const { return _gold; }
	void SetGold(int32 gold) { _gold = gold; }
	void AddGold(int32 gold) { _gold += gold; }

private:
	GameSessionRef _session;
	Protocol::PlayerInfo _playerInfo;
	int32 _gold = 0;
};

using PlayerRef = std::shared_ptr<Player>;
