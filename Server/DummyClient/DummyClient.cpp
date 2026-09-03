#include <iostream>
#include <crtdbg.h>
#include "CorePch.h"
#include "ThreadManager.h"
#include "Service.h"
#include "Session.h"

#include "ClientPacketHandler.h"
#include "DummyScenario.h"

#include <atomic>
static std::atomic<int32> GTicketIdCounter = 0;

class ServerSession : public PacketSession
{
public:
	virtual void OnConnected() override
	{
		/*
		 * [2026-09-04] 티켓 접두사 수정 — 결함 F11
		 *
		 * 변경 전: "test_ticket_<n>"
		 *   서버의 테스트 백도어는 **"dummy" 접두사**만 Redis에 자동 등록합니다.
		 *   "test_ticket_"은 그 조건에 걸리지 않으므로, Redis가 켜져 있으면
		 *   GET이 실패해 서버가 즉시 Disconnect(L"Invalid Ticket") 합니다.
		 *   그리고 OnDisconnected가 1초 뒤 재접속을 걸기 때문에
		 *   **거부 → 재접속 무한 루프**가 됩니다.
		 *   과거 테스트가 통과한 것은 Redis가 꺼져 있어 우회 분기를 탔기 때문입니다.
		 *
		 * 변경 후: "dummy_<PlayerId>"
		 *   서버가 접미사 숫자를 PlayerId로 해석하므로 신원이 결정적으로 고정되고,
		 *   재접속해도 같은 캐릭터로 들어갑니다. 왕복 검증이 가능해집니다.
		 *   (세션이 여러 개면 PlayerId를 1씩 늘려 서로 다른 캐릭터가 되게 합니다.)
		 */
		const uint64 playerId = DummyScenario::GPlayerId + static_cast<uint64>(GTicketIdCounter.fetch_add(1));

		Protocol::C_LOGIN loginPkt;
		loginPkt.set_ticket("dummy_" + std::to_string(playerId));
		Send(ClientPacketHandler::MakeSendBuffer(loginPkt));
	}


	virtual void OnDisconnected() override
	{
		// std::cout << "Disconnected" << std::endl;
		/*
		 * [2026-09-04] 자동 재접속을 옵션으로 강등 — 결함 F11의 2차 피해
		 *   무조건 재접속하면, 로그인이 거부되는 상황에서 거부→재접속 루프가 되어
		 *   서버 콘솔이 도배되고 프로세스가 스스로 종료되지 못합니다.
		 *   시나리오 모드(Move/Verify)는 1회 수행 후 끝나야 하므로 기본을 끔으로 두고,
		 *   수명주기 반복이 목적인 Stress 모드에서만 켭니다.
		 */
		if (DummyScenario::GAutoReconnect == false)
			return;

		std::shared_ptr<Service> service = GetService();
		if (service)
		{
			// 1초 대기 후 새로운 세션을 만들어 재접속 (라이프사이클 반복 테스트)
			std::thread([service]() {
				std::this_thread::sleep_for(std::chrono::milliseconds(1000));
				SessionRef session = service->CreateSession();
				if (session)
				{
					session->Connect();
				}
			}).detach();
		}
	}

	virtual void OnRecvPacket(std::span<std::byte> buffer) override
	{
		PacketSessionRef session = GetPacketSessionRef();
		ClientPacketHandler::HandlePacket(session, buffer);
	}

	virtual void OnSend(int32 len) override
	{
		
	}
};

/*
 * ParseArgs
 * [Phase 4] DummyClient 접속 대상 외부화 (2026-09-03)
 *   과거에는 NetAddress(L"127.0.0.1", 7777)이 하드코딩되어 있어 로컬에서만 쓸 수 있었습니다.
 *   그러나 9/5 데모 시나리오 Step 3은 "외부망에서 DummyClient로 5세션 동시 접속"을 요구하므로,
 *   AWS Elastic IP를 지정할 수 있어야 합니다.
 *
 *   사용법: DummyClient.exe -ip=13.125.10.20 -port=7777 -sessions=5
 *   인수를 생략하면 기존과 동일한 기본값(127.0.0.1:7777, 5세션)으로 동작합니다.
 */
struct DummyClientArgs
{
	std::wstring ip = L"127.0.0.1";
	uint16       port = 7777;
	int32        sessionCount = 1;		// [2026-09-04] 기본을 5 -> 1 로 낮춤 (부하 유발 방지)
	int32        timeoutSeconds = 20;	// 시나리오가 끝나지 않아도 이 시간 뒤 종료
};

static DummyClientArgs ParseArgs(int argc, char* argv[])
{
	DummyClientArgs args;

	for (int32 i = 1; i < argc; i++)
	{
		const std::string arg = argv[i];

		if (arg.starts_with("-ip="))
		{
			const std::string value = arg.substr(4);
			args.ip.assign(value.begin(), value.end()); // ASCII 점 표기 IPv4만 사용하므로 단순 확장으로 충분
		}
		else if (arg.starts_with("-port="))
		{
			const int32 value = std::atoi(arg.substr(6).c_str());
			if (value > 0 && value <= 65535)
				args.port = static_cast<uint16>(value);
			else
				std::cout << "[DummyClient] Ignored invalid -port=" << value << " (must be 1..65535)" << std::endl;
		}
		else if (arg.starts_with("-sessions="))
		{
			const int32 value = std::atoi(arg.substr(10).c_str());
			if (value > 0)
			{
				// [2026-09-04] 하드 상한. 사용자 PC 사양을 초과하면 강제 종료되는 이력이 있어
				// 부하 시험을 구조적으로 차단합니다. 이 상한을 올리지 마십시오.
				if (value > DummyScenario::kMaxSessions)
				{
					std::cout << "[DummyClient] -sessions=" << value << " exceeds hard cap "
					          << DummyScenario::kMaxSessions << ". Clamped." << std::endl;
					args.sessionCount = DummyScenario::kMaxSessions;
				}
				else
				{
					args.sessionCount = value;
				}
			}
			else
			{
				std::cout << "[DummyClient] Ignored invalid -sessions=" << value << " (must be >= 1)" << std::endl;
			}
		}
		else if (arg.starts_with("-scenario="))
		{
			const std::string value = arg.substr(10);
			if (value == "idle")        DummyScenario::GMode = DummyScenario::Mode::Idle;
			else if (value == "move")   DummyScenario::GMode = DummyScenario::Mode::Move;
			else if (value == "verify") DummyScenario::GMode = DummyScenario::Mode::Verify;
			else if (value == "stress") { DummyScenario::GMode = DummyScenario::Mode::Stress; DummyScenario::GAutoReconnect = true; }
			else std::cout << "[DummyClient] Unknown -scenario=" << value << " (idle|move|verify|stress)" << std::endl;
		}
		else if (arg.starts_with("-playerid="))
		{
			const int64 value = std::atoll(arg.substr(10).c_str());
			if (value > 0) DummyScenario::GPlayerId = static_cast<uint64>(value);
		}
		else if (arg.starts_with("-move="))
		{
			// 형식: -move=X,Y,Z
			std::sscanf(arg.substr(6).c_str(), "%f,%f,%f",
			            &DummyScenario::GTargetX, &DummyScenario::GTargetY, &DummyScenario::GTargetZ);
		}
		else if (arg.starts_with("-expect="))
		{
			// 형식: -expect=X,Y,Z
			std::sscanf(arg.substr(8).c_str(), "%f,%f,%f",
			            &DummyScenario::GExpectX, &DummyScenario::GExpectY, &DummyScenario::GExpectZ);
		}
		else if (arg.starts_with("-timeout="))
		{
			const int32 value = std::atoi(arg.substr(9).c_str());
			if (value > 0) args.timeoutSeconds = value;
		}
		else
		{
			std::cout << "[DummyClient] Unknown argument ignored: " << arg << std::endl;
			std::cout << "  usage: DummyClient.exe [-ip=x.x.x.x] [-port=7777] [-sessions=1] [-timeout=20]" << std::endl;
			std::cout << "         [-scenario=idle|move|verify|stress] [-playerid=N]" << std::endl;
			std::cout << "         [-move=X,Y,Z] [-expect=X,Y,Z]" << std::endl;
		}
	}

	return args;
}

int main(int argc, char* argv[])
{
	// 메모리 누수 탐지 (종료 시 덤프)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

	const DummyClientArgs args = ParseArgs(argc, argv);

	ClientPacketHandler::Init();

	// 서버가 켜질 때까지 대기
	std::this_thread::sleep_for(std::chrono::seconds(1));

	std::wcout << L"[DummyClient] Target " << args.ip << L":" << args.port
	           << L"  sessions=" << args.sessionCount << std::endl;

	ClientServiceRef service = std::make_shared<ClientService>(
		NetAddress(args.ip, args.port),
		std::make_shared<IocpCore>(),
		[]() { return std::make_shared<ServerSession>(); }, // Session Factory
		args.sessionCount
	);


	ASSERT_CRASH(service->Start());

	// 비동기 통신을 담당할 워커 스레드 2개 실행
	for (int32 i = 0; i < 2; i++)
	{
		GThreadManager->Launch([=](std::stop_token stopToken)
			{
				while (!stopToken.stop_requested())
				{
					service->GetIocpCore()->Dispatch(10);
				}
			});
	}

	std::cout << "[DummyClient] scenario=" << DummyScenario::ToString(DummyScenario::GMode)
	          << "  playerId=" << DummyScenario::GPlayerId
	          << "  timeout=" << args.timeoutSeconds << "s" << std::endl;

	/*
	 * [2026-09-04] PING 스레드에 종료 조건 추가
	 *   기존에는 while(true)라 프로세스가 끝날 때까지 멈추지 않았고,
	 *   메인 루프도 while(true)여서 **DummyClient가 스스로 종료할 수 없었습니다.**
	 *   자동화 검증 도구로 쓰려면 판정 후 종료 코드를 반환해야 하므로
	 *   공유 종료 플래그로 두 루프를 함께 내립니다.
	 */
	std::atomic<bool> running{ true };

	std::thread pingThread([service, &running]() {
		while (running.load())
		{
			Protocol::C_PING pingPkt;
			service->Broadcast(ClientPacketHandler::MakeSendBuffer(pingPkt));
			// 종료 신호에 빠르게 반응하도록 3초를 잘게 쪼개 대기합니다.
			for (int32 i = 0; i < 30 && running.load(); i++)
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
	});

	// 메인 스레드는 클라이언트 측 작업(JobTimer) 분배를 담당합니다.
	// 시나리오가 끝나거나 타임아웃에 도달하면 루프를 빠져나갑니다.
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(args.timeoutSeconds);
	const bool isScenario = (DummyScenario::GMode == DummyScenario::Mode::Move ||
	                         DummyScenario::GMode == DummyScenario::Mode::Verify);
	bool timedOut = false;

	while (true)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		GJobTimer->Distribute(::GetTickCount64());

		if (isScenario && DummyScenario::GFinished.load())
			break;

		if (std::chrono::steady_clock::now() >= deadline)
		{
			timedOut = true;
			break;
		}
	}

	running.store(false);
	if (pingThread.joinable())
		pingThread.join();

	// ── 판정 요약 ────────────────────────────────────────────────────────────
	std::cout << std::endl;
	std::cout << "================ DummyClient Result ================" << std::endl;
	std::cout << " scenario   : " << DummyScenario::ToString(DummyScenario::GMode) << std::endl;
	std::cout << " login      : " << (DummyScenario::GLoginOk.load() ? "OK" : "FAIL") << std::endl;
	std::cout << " enter game : " << (DummyScenario::GEnterOk.load() ? "OK" : "FAIL") << std::endl;
	std::cout << " playerId   : " << DummyScenario::GReceivedPlayerId.load() << std::endl;
	std::cout << " position   : (" << DummyScenario::GRecvX.load() << ", "
	          << DummyScenario::GRecvY.load() << ", " << DummyScenario::GRecvZ.load() << ")" << std::endl;
	if (timedOut)
		std::cout << " NOTE       : timed out after " << args.timeoutSeconds << "s" << std::endl;

	int32 exitCode = 0;
	if (DummyScenario::GMode == DummyScenario::Mode::Verify)
	{
		const bool passed = DummyScenario::GPassed.load();
		std::cout << " VERDICT    : " << (passed ? "PASS" : "FAIL") << std::endl;
		exitCode = passed ? 0 : 1;
	}
	else if (isScenario)
	{
		const bool ok = DummyScenario::GLoginOk.load() && DummyScenario::GEnterOk.load() && !timedOut;
		std::cout << " VERDICT    : " << (ok ? "OK" : "FAIL") << std::endl;
		exitCode = ok ? 0 : 1;
	}
	std::cout << "===================================================" << std::endl;

	// 워커 스레드 정리 (ThreadManager가 stop_token으로 루프를 내립니다)
	GThreadManager->Join();
	return exitCode;
}
