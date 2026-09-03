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
	/*
	 * MakePlayerInfo
	 * 역할: 패킷 전송이 필요한 순간에만 PlayerInfo를 "조립해서" 반환합니다.
	 *
	 * [O1] 상태 단일화(Single Source of Truth) 조치:
	 *   과거에는 Player가 Protocol::PlayerInfo _playerInfo 멤버를 따로 들고 있었습니다.
	 *   그런데 Protocol::PlayerInfo는 이미 ObjectInfo를 내포(compose)하고 있어,
	 *   GameObject::_info 와 _playerInfo.objectinfo 라는 동일 데이터의 사본이 2개 존재했습니다.
	 *   두 사본은 동기화되지 않았기 때문에:
	 *     - HandleMove()는 _info 를 갱신하는데
	 *     - GetSaveData()는 _playerInfo 를 읽어서 DB에 저장 → 이동 좌표가 영원히 저장되지 않음
	 *     - S_SPAWN은 _info 를 실어 보내는데 _info.name 은 아무도 대입하지 않음 → 타인에게 이름 공백
	 *   이제 상태의 유일한 소유자는 GameObject::_info 이며, PlayerInfo는 여기서 파생됩니다.
	 *   ⚠️ 절대 PlayerInfo를 멤버로 다시 들이지 마세요. 같은 버그가 재발합니다.
	 */
	Protocol::PlayerInfo MakePlayerInfo();
	PlayerSaveData GetSaveData();

	int32 GetLevel() const { return _level; }
	void  SetLevel(int32 level) { _level = level; }

	int32 GetGold() const { return _gold; }
	void SetGold(int32 gold) { _gold = gold; }
	void AddGold(int32 gold) { _gold += gold; }

private:
	GameSessionRef _session;
	// 레벨은 ObjectInfo에 없는 Player 고유 상태이므로 여기서 소유합니다.
	// (기존 코드에서도 어디에서도 대입되지 않아 항상 0이 전송되었으므로 기본값을 0으로 유지합니다.
	//  추후 DB 로드 시 SetLevel()로 채워야 합니다. — 추적 대상)
	int32 _level = 0;
	int32 _gold = 0;
};

using PlayerRef = std::shared_ptr<Player>;
