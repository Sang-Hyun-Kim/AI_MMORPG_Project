#include "ServerPacketHandler.h"
#include <iostream>
#include "GameSession.h"
#include "GameRoom.h"
#include "RedisManager.h"

std::array<PacketHandlerFunc, UINT16_MAX + 1> GPacketHandler; // [S2] 65536칸

bool Handle_INVALID(PacketSessionRef& session, std::span<std::byte> buffer)
{
	return false;
}
bool Handle_C_LOGIN(PacketSessionRef& session, Protocol::C_LOGIN& pkt)
{
	std::wcout << L"[ServerPacketHandler] C_LOGIN Received! Ticket: " << pkt.ticket().c_str() << std::endl;

	std::string ticket = pkt.ticket();
	std::string lowerTicket = ticket;
	std::transform(lowerTicket.begin(), lowerTicket.end(), lowerTicket.begin(), ::tolower);

	if (GRedisManager && GRedisManager->GetRedis())
	{
		std::string key = "Ticket:User:" + ticket;

		// 포트폴리오 테스트용 백도어: dummy 로 시작하면 (대소문자 무관 DummyTicket, dummy_ 모두) 임시 삽입
		if (lowerTicket.starts_with("dummy"))
		{
			GRedisManager->GetRedis()->set(key, "1");
			std::wcout << L"[ServerPacketHandler] Test Ticket Auto-Registered in Redis: " << ticket.c_str() << std::endl;
		}

		// Lua Script 대신 간편하게 Transaction(GET+DEL)을 사용하는 예시
		auto val = GRedisManager->GetRedis()->get(key);
		if (val)
		{
			GRedisManager->GetRedis()->del(key);
			// 검증 성공
			Protocol::S_LOGIN loginPkt;
			loginPkt.set_success(true);
			SendBufferRef sendBuffer = ServerPacketHandler::MakeSendBuffer(loginPkt);
			session->Send(sendBuffer);
			std::wcout << L"[ServerPacketHandler] Login Success! Ticket: " << ticket.c_str() << std::endl;
			return true;
		}
		else
		{
			// 검증 실패
			std::wcout << L"Login Failed: Invalid Ticket (" << ticket.c_str() << L")" << std::endl;
			session->Disconnect(L"Invalid Ticket");
			return false;
		}
	}
	else
	{
		std::wcout << L"[ServerPacketHandler] Redis is offline. Bypassing login validation for testing." << std::endl;
		Protocol::S_LOGIN loginPkt;
		loginPkt.set_success(true);
		SendBufferRef sendBuffer = ServerPacketHandler::MakeSendBuffer(loginPkt);
		session->Send(sendBuffer);
		return true;
	}
	return false;
}
bool Handle_C_ENTER_GAME(PacketSessionRef& session, Protocol::C_ENTER_GAME& pkt)
{
	GameSessionRef gameSession = std::static_pointer_cast<GameSession>(session);
	
	static std::atomic<uint64> idGenerator = 1;
	uint64 tempPlayerId = idGenerator.fetch_add(1);

	// C_ENTER_GAME을 수신했을 때 비로소 플레이어를 생성/로드하고,
	// S_ENTER_GAME 선발송 후 GameRoom::Enter(S_SPAWN)를 실행!
	gameSession->LoadPlayerTask(1, tempPlayerId);
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
bool Handle_C_ATTACK(PacketSessionRef& session, Protocol::C_ATTACK& pkt)
{
	GameSessionRef gameSession = std::static_pointer_cast<GameSession>(session);
	PlayerRef player = gameSession->GetPlayer();
	if (player == nullptr)
		return false;

	GameRoomRef room = player->GetRoom();
	if (room == nullptr)
		return false;

	room->DoAsync(&GameRoom::HandleAttack, player, pkt);
	return true;
}
bool Handle_C_CHECK_MAILBOX(PacketSessionRef& session, Protocol::C_CHECK_MAILBOX& pkt)
{
	GameSessionRef gameSession = std::static_pointer_cast<GameSession>(session);
	PlayerRef player = gameSession->GetPlayer();
	if (player == nullptr)
		return false;

	GameRoomRef room = player->GetRoom();
	if (room == nullptr)
		return false;

	room->DoAsync(&GameRoom::HandleCheckMailbox, player, pkt);
	return true;
}