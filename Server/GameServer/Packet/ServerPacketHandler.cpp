#include "ServerPacketHandler.h"
#include <iostream>

std::array<PacketHandlerFunc, UINT16_MAX> GPacketHandler;

bool Handle_INVALID(PacketSessionRef& session, std::span<std::byte> buffer)
{
	return false;
}
bool Handle_C_LOGIN(PacketSessionRef& session, Protocol::C_LOGIN& pkt)
{
	// std::cout << "[GameServer] C_LOGIN Request Received!" << std::endl;
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
	// std::cout << "[GameServer] C_MOVE Received - Name: " << pkt.info().name() << ", Level: " << pkt.info().level() << std::endl;
	Protocol::S_MOVE movePkt;
	movePkt.mutable_info()->CopyFrom(pkt.info());
	SendBufferRef sendBuffer = ServerPacketHandler::MakeSendBuffer(movePkt);
	session->Send(sendBuffer);
	return true;
}
bool Handle_C_CHAT(PacketSessionRef& session, Protocol::C_CHAT& pkt)
{
	return true;
}