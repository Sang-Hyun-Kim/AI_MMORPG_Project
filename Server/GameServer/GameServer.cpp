#include "ThreadManager.h"
#include "Service.h"
#include "Session.h"
#include "SocketUtils.h"
#include <crtdbg.h>

#include "ServerPacketHandler.h"

#include "GameSession.h"
#include "GameRoomManager.h"
#include "RedisManager.h"

int main()
{
	// 메모리 누수 탐지 (종료 시 덤프)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

	ServerPacketHandler::Init();
	SocketUtils::Init();
	GGameRoomManager->Init();
	
	GRedisManager = std::make_shared<RedisManager>();
	if (!GRedisManager->Connect())
	{
		std::cout << "Redis Connect Failed!" << std::endl;
	}

	ServerServiceRef service = std::make_shared<ServerService>(
		NetAddress(L"127.0.0.1", 7777),
		std::make_shared<IocpCore>(),
		[]() { return std::make_shared<GameSession>(); },
		100);

	ASSERT_CRASH(service->Start());

	// Session Sweeper Thread (1초 주기, C++20 jthread + stop_token)
	GThreadManager->Launch([service](std::stop_token stopToken) {
		while (!stopToken.stop_requested())
		{
			service->SweepSessions();
			std::this_thread::sleep_for(std::chrono::seconds(1));
		}
	});

	for (int32 i = 0; i < 5; i++)
	{
		GThreadManager->Launch([=](std::stop_token stopToken)
		{
			while (!stopToken.stop_requested())
			{
				service->GetIocpCore()->Dispatch(10);
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
