#include <iostream>
#include <crtdbg.h>
#include "CorePch.h"
#include "ThreadManager.h"
#include "Service.h"
#include "Session.h"

#include "ClientPacketHandler.h"

#include <atomic>
static std::atomic<int32> GTicketIdCounter = 0;

class ServerSession : public PacketSession
{
public:
	virtual void OnConnected() override
	{
		// 접속 성공 시 C_LOGIN 패킷 전송
		Protocol::C_LOGIN loginPkt;
		std::string ticket = "test_ticket_" + std::to_string(GTicketIdCounter.fetch_add(1));
		loginPkt.set_ticket(ticket);
		SendBufferRef sendBuffer = ClientPacketHandler::MakeSendBuffer(loginPkt);
		Send(sendBuffer);
	}


	virtual void OnDisconnected() override
	{
		// std::cout << "Disconnected" << std::endl;
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
	int32        sessionCount = 5;
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
				args.sessionCount = value;
			else
				std::cout << "[DummyClient] Ignored invalid -sessions=" << value << " (must be >= 1)" << std::endl;
		}
		else
		{
			std::cout << "[DummyClient] Unknown argument ignored: " << arg << std::endl;
			std::cout << "  usage: DummyClient.exe [-ip=x.x.x.x] [-port=7777] [-sessions=5]" << std::endl;
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

	std::cout << "DummyClient is running..." << std::endl;

	// PING 패킷 전송 스레드 (3초 주기)
	std::thread pingThread([service]() {
		while (true)
		{
			Protocol::C_PING pingPkt;
			SendBufferRef sendBuffer = ClientPacketHandler::MakeSendBuffer(pingPkt);
			service->Broadcast(sendBuffer);
			std::this_thread::sleep_for(std::chrono::seconds(3));
		}
	});

	// 메인 스레드는 클라이언트 측 작업(JobTimer) 분배를 담당
	while (true)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		GJobTimer->Distribute(::GetTickCount64());
	}

	GThreadManager->Join();
	pingThread.join();
	return 0;
}
