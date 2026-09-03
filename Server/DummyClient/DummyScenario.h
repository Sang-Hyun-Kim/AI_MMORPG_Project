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
	inline uint64 GPlayerId = 1;			// 백도어 티켓 dummy_<GPlayerId> 로 신원을 고정
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
