#include "ServerPacketHandler.h"
#include <iostream>
#include "GameSession.h"
#include "GameRoom.h"
#include "RedisManager.h"

std::array<PacketHandlerFunc, UINT16_MAX> GPacketHandler;

bool Handle_INVALID(PacketSessionRef& session, std::span<std::byte> buffer)
{
	return false;
}
bool Handle_C_LOGIN(PacketSessionRef& session, Protocol::C_LOGIN& pkt)
{
	if (GRedisManager && GRedisManager->GetRedis())
	{
		std::string ticket = pkt.ticket();
		std::string key = "Ticket:User:" + ticket;

		// 포트폴리오 테스트용 백도어: dummy_ 로 시작하면 임시 삽입
		if (ticket.starts_with("dummy_"))
		{
			GRedisManager->GetRedis()->set(key, "1");
		}

		// Lua Script 대신 간편하게 Transaction(GET+DEL)을 사용하는 예시 (redis-plus-plus의 pipeline/transaction 활용 또는 단순 get/del)
		// 완벽한 원자성을 위해서는 Lua Script가 좋으나, 여기서는 포트폴리오 시연용으로 간략화
		auto val = GRedisManager->GetRedis()->get(key);
		if (val)
		{
			GRedisManager->GetRedis()->del(key);
			// 검증 성공
			Protocol::S_LOGIN loginPkt;
			loginPkt.set_success(true);
			SendBufferRef sendBuffer = ServerPacketHandler::MakeSendBuffer(loginPkt);
			session->Send(sendBuffer);
			return true;
		}
		else
		{
			// 검증 실패
			std::wcout << L"Login Failed: Invalid Ticket" << std::endl;
			session->Disconnect(L"Invalid Ticket");
			return false;
		}
	}
	return false;
}
bool Handle_C_ENTER_GAME(PacketSessionRef& session, Protocol::C_ENTER_GAME& pkt)
{
	return true;
}
bool Handle_C_LEAVE_GAME(PacketSessionRef& session, Protocol::C_LEAVE_GAME& pkt)
{
	return true;
}
bool Handle_C_MOVE(PacketSessionRef& session, Protocol::C_MOVE& pkt)
{
	GameSessionRef gameSession = std::static_pointer_cast<GameSession>(session);
	PlayerRef player = gameSession->GetPlayer();
	if (player == nullptr)
		return false;

	GameRoomRef room = player->GetRoom();
	if (room == nullptr)
		return false;

	room->DoAsync(&GameRoom::HandleMove, player, pkt);
	return true;
}
bool Handle_C_CHAT(PacketSessionRef& session, Protocol::C_CHAT& pkt)
{
	return true;
}
bool Handle_C_PING(PacketSessionRef& session, Protocol::C_PING& pkt)
{
	Protocol::S_PONG pongPkt;
	SendBufferRef sendBuffer = ServerPacketHandler::MakeSendBuffer(pongPkt);
	session->Send(sendBuffer);
	return true;
}