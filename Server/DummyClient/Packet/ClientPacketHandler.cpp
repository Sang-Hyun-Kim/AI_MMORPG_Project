#include "ClientPacketHandler.h"
#include <iostream>
#include <thread>
#include "ThreadManager.h"
#include "Session.h"
#include "CoreGlobal.h"
#include "JobTimer.h"

std::array<PacketHandlerFunc, UINT16_MAX> GPacketHandler;

bool Handle_INVALID(PacketSessionRef& session, std::span<std::byte> buffer)
{
	return false;
}

void DoStressTest(PacketSessionRef session)
{
	if (session == nullptr)
		return;

	std::thread([session]() {
		// 메모리/핸들 누수 검증을 위해 접속 중 5번만 C_MOVE 패킷을 전송하고 접속을 종료합니다.
		for (int32 i = 0; i < 5; i++)
		{
			if (session->IsConnected() == false)
				break;

			Protocol::C_MOVE movePkt;
			movePkt.mutable_info()->set_name("Dummy");
			movePkt.mutable_info()->set_level(10);
			
			SendBufferRef sendBuffer = ClientPacketHandler::MakeSendBuffer(movePkt);
			session->Send(sendBuffer);

			// 과부하 방지 및 패킷 처리 여유 시간을 위해 500ms 대기
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
		}

		// 테스트용 통신이 끝나면 연결을 직접 끊어서 리소스 해제(Disconnect) 유도
		if (session->IsConnected())
		{
			session->Disconnect(L"Test Lifecycle Complete");
		}
	}).detach();
}

bool Handle_S_LOGIN(PacketSessionRef& session, Protocol::S_LOGIN& pkt)
{
	if (pkt.success())
	{
		// std::cout << "[DummyClient] Login Success! Starting Stress Test..." << std::endl;
		DoStressTest(session);
	}
	return true;
}
bool Handle_S_ENTER_GAME(PacketSessionRef& session, Protocol::S_ENTER_GAME& pkt)
{
	return true;
}
bool Handle_S_LEAVE_GAME(PacketSessionRef& session, Protocol::S_LEAVE_GAME& pkt)
{
	return true;
}
bool Handle_S_SPAWN(PacketSessionRef& session, Protocol::S_SPAWN& pkt)
{
	return true;
}
bool Handle_S_DESPAWN(PacketSessionRef& session, Protocol::S_DESPAWN& pkt)
{
	return true;
}
bool Handle_S_MOVE(PacketSessionRef& session, Protocol::S_MOVE& pkt)
{
	// std::cout << "[DummyClient] Echo S_MOVE Received - Name: " << pkt.info().name() << ", Level: " << pkt.info().level() << std::endl;
	return true;
}
bool Handle_S_CHAT(PacketSessionRef& session, Protocol::S_CHAT& pkt)
{
	return true;
}