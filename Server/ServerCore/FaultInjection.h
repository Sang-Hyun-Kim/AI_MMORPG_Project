#pragma once
#include <atomic>
#include <stdexcept>

/*
 * FaultInjection — 예외 경계 시험용 실패 주입 훅 [TD-02]  (정책·사용법: Docs/Code_Specification.md 18장)
 *   환경 변수 MMO_FAULT=DbJob:3,PacketHandler:1 → 해당 지점에서 N회만 FaultInjected 를 던집니다.
 *
 * ⚠️ Debug 전용입니다. Release 에서 MMO_FAULT_POINT 는 ((void)0) 이며 Init 은 환경 변수를 읽지 않습니다.
 *    삽입 지점은 반드시 그 예외를 받는 경계(catch)와 같은 커밋에 넣으십시오.
 */

enum class FaultPoint : uint8
{
	PacketHandler,  // Handle_C_ENTER_GAME 진입 → PacketSession::OnRecv 경계
	JobExecute,     // GameRoom::HandleMove 진입 → JobQueue::Execute 경계
	DbJob,          // LoadPlayerTask DB 람다 → DBConnectionPool 워커 · DBAwaitable 경계
	CoroutineBody,  // LoadPlayerTask 재개 직후 → JobTask::promise_type::unhandled_exception
	Count
};

class FaultInjection
{
public:
	static void Init() noexcept;                    // main() 에서 1회 (Logger::Configure 뒤)
	static bool ShouldFire(FaultPoint p) noexcept;  // 남은 횟수가 있으면 1 감소 후 true

private:
	static std::atomic<int32> sRemaining[static_cast<int>(FaultPoint::Count)];
};

struct FaultInjected : std::runtime_error
{
	using std::runtime_error::runtime_error;
};

#ifdef _DEBUG
#define MMO_FAULT_POINT(p)                                                                     \
	do                                                                                         \
	{                                                                                          \
		if (::FaultInjection::ShouldFire(::FaultPoint::p))                                     \
			throw ::FaultInjected("fault injected: " #p);                                      \
	} while (0)
#else
#define MMO_FAULT_POINT(p) ((void)0)
#endif
