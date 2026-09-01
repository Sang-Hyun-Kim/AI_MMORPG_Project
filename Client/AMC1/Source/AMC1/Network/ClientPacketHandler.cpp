#include "ClientPacketHandler.h"
#include "../AMC1.h" // For UE_LOG, etc.
#include "Engine/Engine.h"

std::array<PacketHandlerFunc, UINT16_MAX> GPacketHandler;

bool Handle_INVALID(PacketSessionRef& session, std::span<std::byte> buffer)
{
	UE_LOG(LogTemp, Error, TEXT("[PacketHandler] Handle_INVALID called."));
	return false;
}

bool Handle_S_LOGIN(PacketSessionRef& session, Protocol::S_LOGIN& pkt)
{
	UE_LOG(LogTemp, Log, TEXT("[PacketHandler] S_LOGIN Received! Success: %d"), pkt.success());
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Cyan, FString::Printf(TEXT("[Packet] S_LOGIN (Success: %d)"), pkt.success()));
	return true;
}

bool Handle_S_ENTER_GAME(PacketSessionRef& session, Protocol::S_ENTER_GAME& pkt)
{
	UE_LOG(LogTemp, Log, TEXT("[PacketHandler] S_ENTER_GAME Received!"));
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Cyan, TEXT("[Packet] S_ENTER_GAME"));
	return true;
}

bool Handle_S_LEAVE_GAME(PacketSessionRef& session, Protocol::S_LEAVE_GAME& pkt)
{
	UE_LOG(LogTemp, Log, TEXT("[PacketHandler] S_LEAVE_GAME Received!"));
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Cyan, TEXT("[Packet] S_LEAVE_GAME"));
	return true;
}

bool Handle_S_SPAWN(PacketSessionRef& session, Protocol::S_SPAWN& pkt)
{
	UE_LOG(LogTemp, Log, TEXT("[PacketHandler] S_SPAWN Received!"));
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Cyan, TEXT("[Packet] S_SPAWN"));
	return true;
}

bool Handle_S_DESPAWN(PacketSessionRef& session, Protocol::S_DESPAWN& pkt)
{
	UE_LOG(LogTemp, Log, TEXT("[PacketHandler] S_DESPAWN Received!"));
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Cyan, TEXT("[Packet] S_DESPAWN"));
	return true;
}

bool Handle_S_MOVE(PacketSessionRef& session, Protocol::S_MOVE& pkt)
{
	UE_LOG(LogTemp, Log, TEXT("[PacketHandler] S_MOVE Received!"));
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Cyan, TEXT("[Packet] S_MOVE"));
	return true;
}

bool Handle_S_CHAT(PacketSessionRef& session, Protocol::S_CHAT& pkt)
{
	UE_LOG(LogTemp, Log, TEXT("[PacketHandler] S_CHAT Received!"));
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Cyan, TEXT("[Packet] S_CHAT"));
	return true;
}

bool Handle_S_PONG(PacketSessionRef& session, Protocol::S_PONG& pkt)
{
	UE_LOG(LogTemp, Log, TEXT("[PacketHandler] S_PONG Received!"));
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Cyan, TEXT("[Packet] S_PONG"));
	return true;
}

bool Handle_S_ATTACK(PacketSessionRef& session, Protocol::S_ATTACK& pkt)
{
	UE_LOG(LogTemp, Log, TEXT("[PacketHandler] S_ATTACK Received!"));
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Cyan, TEXT("[Packet] S_ATTACK"));
	return true;
}

bool Handle_S_STATUS_CHANGE(PacketSessionRef& session, Protocol::S_STATUS_CHANGE& pkt)
{
	UE_LOG(LogTemp, Log, TEXT("[PacketHandler] S_STATUS_CHANGE Received!"));
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Cyan, TEXT("[Packet] S_STATUS_CHANGE"));
	return true;
}
