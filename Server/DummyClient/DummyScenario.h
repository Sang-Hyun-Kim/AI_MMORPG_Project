#pragma once
#include "CorePch.h"
#include <atomic>
#include <string>

/*
 * DummyScenario
 * ─────────────────────────────────────────────────────────────────────────────
 * DummyClient의 동작 모드와 판정 결과를 담는 전역 상태입니다.
 * main()이 커맨드라인으로 채우고, ClientPacketHandler의 수신 핸들러가 읽습니다.
 *
 * [왜 만들었나 — 2026-09-04]
 *   DummyClient는 원래 "접속을 반복해 누수를 확인하는" 부하/수명주기 도구였고,
 *   합격/불합격을 스스로 판정하지 못했습니다. Q16 승인에 따라 P3 신원 결속의
 *   왕복(저장 → 재접속 → 복원)을 **UE 없이** 자동 검증할 수 있도록 시나리오
 *   모드를 도입했습니다.
 *
 * [부하 제한 — 사용자 지정]
 *   PC 사양을 초과하면 강제 종료되므로 세션 수 상한을 코드에 못 박았습니다.
 *   kMaxSessions 를 넘겨도 그 값으로 잘립니다. 부하 시험은 이 도구의 목적이 아닙니다.
 */

namespace DummyScenario
{
	// 사용자 환경 보호를 위한 하드 상한. 이 값을 올리지 마십시오.
	// (1:10 이하 서버:클라 비율만 허용 — 그 이상은 PC 강제 종료 이력이 있습니다)
	inline constexpr int32 kMaxSessions = 10;

	enum class Mode
	{
		Idle,		// 접속·로그인·입장만 하고 유지 (연결성 확인)
		Move,		// 입장 후 지정 좌표로 이동시키고 정상 종료 (저장 유도)
		Verify,		// 입장 시 수신한 좌표를 기대값과 대조해 PASS/FAIL 판정
		Stress,		// 기존 수명주기 반복 테스트 (누수 검증)
	};

	// ── main()이 채우는 설정 ────────────────────────────────────────────────
	inline Mode  GMode = Mode::Idle;
	inline uint64 GPlayerId = 1;			// Redis 에 등록하는 티켓 dummy_<GPlayerId> 로 신원을 고정
	inline float GTargetX = 0.f;			// Move 모드에서 이동할 좌표
	inline float GTargetY = 0.f;
	inline float GTargetZ = 0.f;
	inline float GExpectX = 0.f;			// Verify 모드에서 기대하는 좌표
	inline float GExpectY = 0.f;
	inline float GExpectZ = 0.f;
	inline float GTolerance = 1.0f;			// 부동소수 비교 허용 오차
	inline bool  GAutoReconnect = false;	// 끊긴 뒤 자동 재접속 여부 (Stress 전용)

	// ── 핸들러가 채우는 결과 ────────────────────────────────────────────────
	inline std::atomic<bool>  GLoginOk{ false };
	inline std::atomic<bool>  GEnterOk{ false };
	inline std::atomic<bool>  GFinished{ false };	// 시나리오 종료 신호
	inline std::atomic<bool>  GPassed{ false };		// Verify 판정 결과
	inline std::atomic<uint64> GReceivedPlayerId{ 0 };
	inline std::atomic<int32> GRecvX{ 0 };			// float를 정수로 반올림해 보관 (atomic<float> 회피)
	inline std::atomic<int32> GRecvY{ 0 };
	inline std::atomic<int32> GRecvZ{ 0 };

	/*
	 * [2026-09-06] GMoveAcked — "보냈다"와 "서버가 처리했다"를 구분하기 위한 신호
	 *
	 * [무엇이 문제였나]
	 *   Move 시나리오는 S_ENTER_GAME 을 받자마자 C_MOVE 를 1회 보내고 800ms 뒤 끊었습니다.
	 *   그런데 서버는 S_ENTER_GAME 을 **먼저 보내고**(선발송) GameRoom::Enter 를 큐에 넣습니다.
	 *   Enter 가 아직 실행되지 않았으면 Handle_C_MOVE 의 player->GetRoom() 이 nullptr 이라
	 *   **아무 로그 없이 return false** 로 버려집니다.
	 *   결과: 클라이언트는 "C_MOVE sent" 를 찍었는데 DB 에는 (0,0,0) 이 저장되고,
	 *   이어지는 verify 시나리오가 거짓 실패했습니다.
	 *
	 * [해결]
	 *   서버가 브로드캐스트하는 S_MOVE 를 **자기 자신도 받는다**는 점을 이용합니다
	 *   (이동한 본인은 언제나 자기 AOI 안에 있음). 그 에코를 받을 때까지 재전송하고,
	 *   받은 뒤에 끊습니다. 즉 판정 기준이 "송신"에서 "서버 반영 확인"으로 바뀝니다.
	 *
	 * ⚠️ 이 플래그를 지우고 다시 "1회 송신 후 대기"로 되돌리지 마십시오.
	 *    입장 직후 구간에서 간헐적으로 이동이 유실되어 verify 가 거짓 실패합니다.
	 */
	inline std::atomic<bool>  GMoveAcked{ false };

	inline const char* ToString(Mode m)
	{
		switch (m)
		{
		case Mode::Idle:   return "idle";
		case Mode::Move:   return "move";
		case Mode::Verify: return "verify";
		case Mode::Stress: return "stress";
		}
		return "unknown";
	}
}
