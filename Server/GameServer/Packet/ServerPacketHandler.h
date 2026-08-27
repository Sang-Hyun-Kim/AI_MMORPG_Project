#pragma once
#include "Protocol.pb.h"
#include "CorePch.h"
#include "Session.h"
#include "SendBuffer.h"
#include <array>
#include <span>
#include <functional>

#if UE_BUILD_DEBUG + UE_BUILD_DEVELOPMENT + UE_BUILD_TEST + UE_BUILD_SHIPPING >= 1
#include "S1.h"
#endif

// C++20: std::span을 활용하여 버퍼 오버플로우를 방지하는 모던 핸들러 시그니처
using PacketHandlerFunc = std::function<bool(PacketSessionRef&, std::span<std::byte>)>;
extern std::array<PacketHandlerFunc, UINT16_MAX> GPacketHandler;

// C++20: enum class를 통한 강력한 타입 체크
enum class PacketID : uint16
{
	PKT_C_LOGIN = 1000,
	PKT_S_LOGIN = 1001,
	PKT_C_ENTER_GAME = 1002,
	PKT_S_ENTER_GAME = 1003,
	PKT_C_LEAVE_GAME = 1004,
	PKT_S_LEAVE_GAME = 1005,
	PKT_S_SPAWN = 1006,
	PKT_S_DESPAWN = 1007,
	PKT_C_MOVE = 1008,
	PKT_S_MOVE = 1009,
	PKT_C_CHAT = 1010,
	PKT_S_CHAT = 1011,
	PKT_C_PING = 1012,
	PKT_S_PONG = 1013,
};

// Custom Handlers
bool Handle_INVALID(PacketSessionRef& session, std::span<std::byte> buffer);
bool Handle_C_LOGIN(PacketSessionRef& session, Protocol::C_LOGIN& pkt);
bool Handle_C_ENTER_GAME(PacketSessionRef& session, Protocol::C_ENTER_GAME& pkt);
bool Handle_C_LEAVE_GAME(PacketSessionRef& session, Protocol::C_LEAVE_GAME& pkt);
bool Handle_C_MOVE(PacketSessionRef& session, Protocol::C_MOVE& pkt);
bool Handle_C_CHAT(PacketSessionRef& session, Protocol::C_CHAT& pkt);
bool Handle_C_PING(PacketSessionRef& session, Protocol::C_PING& pkt);

class ServerPacketHandler
{
public:
	static void Init()
	{
		for (int32 i = 0; i < UINT16_MAX; i++)
			GPacketHandler[i] = Handle_INVALID;
		GPacketHandler[static_cast<uint16>(PacketID::PKT_C_LOGIN)] = [](PacketSessionRef& session, std::span<std::byte> buffer) { return HandlePacket<Protocol::C_LOGIN>(Handle_C_LOGIN, session, buffer); };
		GPacketHandler[static_cast<uint16>(PacketID::PKT_C_ENTER_GAME)] = [](PacketSessionRef& session, std::span<std::byte> buffer) { return HandlePacket<Protocol::C_ENTER_GAME>(Handle_C_ENTER_GAME, session, buffer); };
		GPacketHandler[static_cast<uint16>(PacketID::PKT_C_LEAVE_GAME)] = [](PacketSessionRef& session, std::span<std::byte> buffer) { return HandlePacket<Protocol::C_LEAVE_GAME>(Handle_C_LEAVE_GAME, session, buffer); };
		GPacketHandler[static_cast<uint16>(PacketID::PKT_C_MOVE)] = [](PacketSessionRef& session, std::span<std::byte> buffer) { return HandlePacket<Protocol::C_MOVE>(Handle_C_MOVE, session, buffer); };
		GPacketHandler[static_cast<uint16>(PacketID::PKT_C_CHAT)] = [](PacketSessionRef& session, std::span<std::byte> buffer) { return HandlePacket<Protocol::C_CHAT>(Handle_C_CHAT, session, buffer); };
		GPacketHandler[static_cast<uint16>(PacketID::PKT_C_PING)] = [](PacketSessionRef& session, std::span<std::byte> buffer) { return HandlePacket<Protocol::C_PING>(Handle_C_PING, session, buffer); };
	}

	static bool HandlePacket(PacketSessionRef& session, std::span<std::byte> buffer)
	{
		PacketHeader* header = reinterpret_cast<PacketHeader*>(buffer.data());
		return GPacketHandler[header->id](session, buffer);
	}
	static SendBufferRef MakeSendBuffer(Protocol::S_LOGIN& pkt) { return MakeSendBuffer(pkt, static_cast<uint16>(PacketID::PKT_S_LOGIN)); }
	static SendBufferRef MakeSendBuffer(Protocol::S_ENTER_GAME& pkt) { return MakeSendBuffer(pkt, static_cast<uint16>(PacketID::PKT_S_ENTER_GAME)); }
	static SendBufferRef MakeSendBuffer(Protocol::S_LEAVE_GAME& pkt) { return MakeSendBuffer(pkt, static_cast<uint16>(PacketID::PKT_S_LEAVE_GAME)); }
	static SendBufferRef MakeSendBuffer(Protocol::S_SPAWN& pkt) { return MakeSendBuffer(pkt, static_cast<uint16>(PacketID::PKT_S_SPAWN)); }
	static SendBufferRef MakeSendBuffer(Protocol::S_DESPAWN& pkt) { return MakeSendBuffer(pkt, static_cast<uint16>(PacketID::PKT_S_DESPAWN)); }
	static SendBufferRef MakeSendBuffer(Protocol::S_MOVE& pkt) { return MakeSendBuffer(pkt, static_cast<uint16>(PacketID::PKT_S_MOVE)); }
	static SendBufferRef MakeSendBuffer(Protocol::S_CHAT& pkt) { return MakeSendBuffer(pkt, static_cast<uint16>(PacketID::PKT_S_CHAT)); }
	static SendBufferRef MakeSendBuffer(Protocol::S_PONG& pkt) { return MakeSendBuffer(pkt, static_cast<uint16>(PacketID::PKT_S_PONG)); }

private:
	template<typename PacketType, typename ProcessFunc>
	static bool HandlePacket(ProcessFunc func, PacketSessionRef& session, std::span<std::byte> buffer)
	{
		PacketType pkt;
		// C++20 std::span을 활용한 안전한 길이 계산
		if (pkt.ParseFromArray(buffer.data() + sizeof(PacketHeader), static_cast<int32>(buffer.size()) - sizeof(PacketHeader)) == false)
			return false;

		return func(session, pkt);
	}

	template<typename T>
	static SendBufferRef MakeSendBuffer(T& pkt, uint16 pktId)
	{
		const uint16 dataSize = static_cast<uint16>(pkt.ByteSizeLong());
		const uint16 packetSize = dataSize + sizeof(PacketHeader);

#if UE_BUILD_DEBUG + UE_BUILD_DEVELOPMENT + UE_BUILD_TEST + UE_BUILD_SHIPPING >= 1
		SendBufferRef sendBuffer = MakeShared<SendBuffer>(packetSize);
#else
		SendBufferRef sendBuffer = make_shared<SendBuffer>(packetSize);
#endif

		PacketHeader* header = reinterpret_cast<PacketHeader*>(sendBuffer->Buffer().data());
		header->size = packetSize;
		header->id = pktId;
		pkt.SerializeToArray(&header[1], dataSize);
		sendBuffer->Close(packetSize);

		return sendBuffer;
	}
};