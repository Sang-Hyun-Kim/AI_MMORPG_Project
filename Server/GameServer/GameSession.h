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

	/*
	 * LoadPlayerTask
	 * DB에서 playerId에 해당하는 캐릭터를 읽어 Player 객체를 만들고,
	 * S_ENTER_GAME 송신 → GameRoom 입장까지 수행하는 코루틴입니다.
	 *
	 * [2026-09-04 시그니처 변경]
	 *   변경 전: LoadPlayerTask(uint64 accountId, uint64 playerId)
	 *            accountId는 호출부에서 상수 1로 고정되어 있었고 본문에서 쓰이지 않았습니다.
	 *   변경 후: LoadPlayerTask(uint64 playerId)
	 *            신원은 C_LOGIN 단계에서 티켓으로 결속된 _playerId 하나로 통일했습니다.
	 *            계정↔캐릭터 매핑은 C# 백엔드의 책임이며, 게임 서버는 계정을 모릅니다.
	 */
	JobTask LoadPlayerTask(uint64 playerId);

public:
	PlayerRef GetPlayer() { return _player; }
	void SetPlayer(PlayerRef player) { _player = player; }

	/*
	 * PlayerId — 이 세션이 조종할 캐릭터의 DB 식별자
	 *
	 * [설정 시점] Handle_C_LOGIN에서 Redis 티켓(Ticket:User:<ticket>)의 값을 읽어 대입합니다.
	 *   그 값은 C# WebBackend가 로그인 성공 시 넣어둔 PlayerId입니다.
	 *   (테스트 백도어 경로에서는 티켓 접미사의 숫자가 쓰입니다. 예: dummy_1024 → 1024)
	 *
	 * [0의 의미] 아직 로그인하지 않았음. Handle_C_ENTER_GAME이 이 값으로
	 *   "로그인 없이 입장 시도"를 차단합니다.
	 *
	 * [과거 방식과의 차이 — 결함 F7 1번째 겹]
	 *   과거에는 Handle_C_ENTER_GAME이 static idGenerator.fetch_add(1)로 ID를 발급했습니다.
	 *   접속할 때마다, 서버를 재시작할 때마다 다른 ID가 나왔기 때문에
	 *   "저장했다가 다시 불러온다"는 왕복이 구조적으로 성립할 수 없었습니다.
	 */
	uint64 GetPlayerId() const { return _playerId; }
	void   SetPlayerId(uint64 playerId) { _playerId = playerId; }

private:
	PlayerRef _player;
	uint64    _playerId = 0;
};
