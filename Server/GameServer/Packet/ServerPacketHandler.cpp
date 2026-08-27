#include "ServerPacketHandler.h"
#include <iostream>
#include "GameSession.h"
#include "GameRoom.h"

std::array<PacketHandlerFunc, UINT16_MAX> GPacketHandler;

bool Handle_INVALID(PacketSessionRef& session, std::span<std::byte> buffer)
{
	return false;
}
bool Handle_C_LOGIN(PacketSessionRef& session, Protocol::C_LOGIN& pkt)
{
	Protocol::S_LOGIN loginPkt;
	loginPkt.set_success(true);
	SendBufferRef sendBuffer = ServerPacketHandler::MakeSendBuffer(loginPkt);
	session->Send(sendBuffer);
	return true;
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