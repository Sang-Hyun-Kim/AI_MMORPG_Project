#include <iostream>
#include <crtdbg.h>
#include "CorePch.h"
#include "ThreadManager.h"
#include "Service.h"
#include "Session.h"

#include "ClientPacketHandler.h"

class ServerSession : public PacketSession
{
public:
	virtual void OnConnected() override
	{
		// 접속 성공 시 C_LOGIN 패킷 전송
		Protocol::C_LOGIN loginPkt;
		loginPkt.set_ticket("dummy_ticket_test");
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

int main()
{
	// 메모리 누수 탐지 (종료 시 덤프)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

	ClientPacketHandler::Init();

	// 서버가 켜질 때까지 대기
	std::this_thread::sleep_for(std::chrono::seconds(1));

	ClientServiceRef service = std::make_shared<ClientService>(
		NetAddress(L"127.0.0.1", 7777),
		std::make_shared<IocpCore>(),
		[]() { return std::make_shared<ServerSession>(); }, // Session Factory
		50 // 라이프사이클 테스트용 50개 접속
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
