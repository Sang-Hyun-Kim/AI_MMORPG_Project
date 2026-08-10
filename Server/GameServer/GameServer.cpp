#include "ThreadManager.h"
#include "Service.h"
#include "Session.h"
#include "SocketUtils.h"
#include <crtdbg.h>

#include "ServerPacketHandler.h"

class GameSession : public PacketSession
{
public:
	virtual void OnConnected() override
	{
		std::wcout << L"GameSession Connected" << std::endl;
	}

	virtual void OnDisconnected() override
	{
		std::wcout << L"GameSession Disconnected" << std::endl;
	}

	virtual void OnRecvPacket(std::span<std::byte> buffer) override
	{
		PacketSessionRef session = GetPacketSessionRef();
		ServerPacketHandler::HandlePacket(session, buffer);
	}

	virtual void OnSend(int32 len) override
	{
		// std::wcout << L"OnSend: " << len << L" bytes" << std::endl;
	}
};

int main()
{
	// 메모리 누수 탐지 (종료 시 덤프)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

	ServerPacketHandler::Init();
	SocketUtils::Init();

	ServerServiceRef service = std::make_shared<ServerService>(
		NetAddress(L"127.0.0.1", 7777),
		std::make_shared<IocpCore>(),
		[]() { return std::make_shared<GameSession>(); },
		100);

	ASSERT_CRASH(service->Start());

	for (int32 i = 0; i < 5; i++)
	{
		GThreadManager->Launch([=](std::stop_token stopToken)
			{
				while (!stopToken.stop_requested())
				{
					service->GetIocpCore()->Dispatch(10);
					
					// 예약된 JobTimer 분배 (메인루프 혹은 전담 워커에서 수행)
					// GJobTimer->Distribute(::GetTickCount64());
				}
			});
	}

	std::cout << "GameServer is running on port 7777..." << std::endl;
	
	while (true)
	{
		GJobTimer->Distribute(::GetTickCount64());
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	GThreadManager->Join();
	SocketUtils::Clear();
	return 0;
}
