#pragma once


#if UE_BUILD_DEBUG + UE_BUILD_DEVELOPMENT + UE_BUILD_TEST + UE_BUILD_SHIPPING >= 1
#include "CoreMinimal.h"
#pragma push_macro("check")
#pragma push_macro("verify")
#pragma push_macro("ensure")
#pragma push_macro("cast")
#undef check
#undef verify
#undef ensure
#undef cast
THIRD_PARTY_INCLUDES_START
#pragma warning(push)
#pragma warning(disable: 4127 4668 4800 4583 4582 5054 4244 4267 4946 4125 4647)
#pragma pack(push, 8)
#include "Protocol.pb.h"
#pragma pack(pop)
#pragma warning(pop)
THIRD_PARTY_INCLUDES_END
#pragma pop_macro("cast")
#pragma pop_macro("ensure")
#pragma pop_macro("verify")
#pragma pop_macro("check")
#else
#include "Protocol.pb.h"
#include "CorePch.h"
#include "Session.h"
#include "SendBuffer.h"
#endif


#include <array>
#include <span>
#include <functional>


#if UE_BUILD_DEBUG + UE_BUILD_DEVELOPMENT + UE_BUILD_TEST + UE_BUILD_SHIPPING >= 1
#include "../AMC1.h"
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
	PKT_C_ATTACK = 1014,
	PKT_S_ATTACK = 1015,
	PKT_S_STATUS_CHANGE = 1016,
	PKT_C_CHECK_MAILBOX = 1017,
};

// Custom Handlers
bool Handle_INVALID(PacketSessionRef& session, std::span<std::byte> buffer);
bool Handle_S_LOGIN(PacketSessionRef& session, Protocol::S_LOGIN& pkt);
bool Handle_S_ENTER_GAME(PacketSessionRef& session, Protocol::S_ENTER_GAME& pkt);
bool Handle_S_LEAVE_GAME(PacketSessionRef& session, Protocol::S_LEAVE_GAME& pkt);
bool Handle_S_SPAWN(PacketSessionRef& session, Protocol::S_SPAWN& pkt);
bool Handle_S_DESPAWN(PacketSessionRef& session, Protocol::S_DESPAWN& pkt);
bool Handle_S_MOVE(PacketSessionRef& session, Protocol::S_MOVE& pkt);
bool Handle_S_CHAT(PacketSessionRef& session, Protocol::S_CHAT& pkt);
bool Handle_S_PONG(PacketSessionRef& session, Protocol::S_PONG& pkt);
bool Handle_S_ATTACK(PacketSessionRef& session, Protocol::S_ATTACK& pkt);
bool Handle_S_STATUS_CHANGE(PacketSessionRef& session, Protocol::S_STATUS_CHANGE& pkt);

class ClientPacketHandler
{
public:
	static void Init()
	{
		for (int32 i = 0; i < UINT16_MAX; i++)
			GPacketHandler[i] = Handle_INVALID;
		GPacketHandler[static_cast<uint16>(PacketID::PKT_S_LOGIN)] = [](PacketSessionRef& session, std::span<std::byte> buffer) { return HandlePacket<Protocol::S_LOGIN>(Handle_S_LOGIN, session, buffer); };
		GPacketHandler[static_cast<uint16>(PacketID::PKT_S_ENTER_GAME)] = [](PacketSessionRef& session, std::span<std::byte> buffer) { return HandlePacket<Protocol::S_ENTER_GAME>(Handle_S_ENTER_GAME, session, buffer); };
		GPacketHandler[static_cast<uint16>(PacketID::PKT_S_LEAVE_GAME)] = [](PacketSessionRef& session, std::span<std::byte> buffer) { return HandlePacket<Protocol::S_LEAVE_GAME>(Handle_S_LEAVE_GAME, session, buffer); };
		GPacketHandler[static_cast<uint16>(PacketID::PKT_S_SPAWN)] = [](PacketSessionRef& session, std::span<std::byte> buffer) { return HandlePacket<Protocol::S_SPAWN>(Handle_S_SPAWN, session, buffer); };
		GPacketHandler[static_cast<uint16>(PacketID::PKT_S_DESPAWN)] = [](PacketSessionRef& session, std::span<std::byte> buffer) { return HandlePacket<Protocol::S_DESPAWN>(Handle_S_DESPAWN, session, buffer); };
		GPacketHandler[static_cast<uint16>(PacketID::PKT_S_MOVE)] = [](PacketSessionRef& session, std::span<std::byte> buffer) { return HandlePacket<Protocol::S_MOVE>(Handle_S_MOVE, session, buffer); };
		GPacketHandler[static_cast<uint16>(PacketID::PKT_S_CHAT)] = [](PacketSessionRef& session, std::span<std::byte> buffer) { return HandlePacket<Protocol::S_CHAT>(Handle_S_CHAT, session, buffer); };
		GPacketHandler[static_cast<uint16>(PacketID::PKT_S_PONG)] = [](PacketSessionRef& session, std::span<std::byte> buffer) { return HandlePacket<Protocol::S_PONG>(Handle_S_PONG, session, buffer); };
		GPacketHandler[static_cast<uint16>(PacketID::PKT_S_ATTACK)] = [](PacketSessionRef& session, std::span<std::byte> buffer) { return HandlePacket<Protocol::S_ATTACK>(Handle_S_ATTACK, session, buffer); };
		GPacketHandler[static_cast<uint16>(PacketID::PKT_S_STATUS_CHANGE)] = [](PacketSessionRef& session, std::span<std::byte> buffer) { return HandlePacket<Protocol::S_STATUS_CHANGE>(Handle_S_STATUS_CHANGE, session, buffer); };
	}

	static bool HandlePacket(PacketSessionRef& session, std::span<std::byte> buffer)
	{
		PacketHeader* header = reinterpret_cast<PacketHeader*>(buffer.data());
		return GPacketHandler[header->id](session, buffer);
	}
	static SendBufferRef MakeSendBuffer(Protocol::C_LOGIN& pkt) { return MakeSendBuffer(pkt, static_cast<uint16>(PacketID::PKT_C_LOGIN)); }
	static SendBufferRef MakeSendBuffer(Protocol::C_ENTER_GAME& pkt) { return MakeSendBuffer(pkt, static_cast<uint16>(PacketID::PKT_C_ENTER_GAME)); }
	static SendBufferRef MakeSendBuffer(Protocol::C_LEAVE_GAME& pkt) { return MakeSendBuffer(pkt, static_cast<uint16>(PacketID::PKT_C_LEAVE_GAME)); }
	static SendBufferRef MakeSendBuffer(Protocol::C_MOVE& pkt) { return MakeSendBuffer(pkt, static_cast<uint16>(PacketID::PKT_C_MOVE)); }
	static SendBufferRef MakeSendBuffer(Protocol::C_CHAT& pkt) { return MakeSendBuffer(pkt, static_cast<uint16>(PacketID::PKT_C_CHAT)); }
	static SendBufferRef MakeSendBuffer(Protocol::C_PING& pkt) { return MakeSendBuffer(pkt, static_cast<uint16>(PacketID::PKT_C_PING)); }
	static SendBufferRef MakeSendBuffer(Protocol::C_ATTACK& pkt) { return MakeSendBuffer(pkt, static_cast<uint16>(PacketID::PKT_C_ATTACK)); }
	static SendBufferRef MakeSendBuffer(Protocol::C_CHECK_MAILBOX& pkt) { return MakeSendBuffer(pkt, static_cast<uint16>(PacketID::PKT_C_CHECK_MAILBOX)); }

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

#if UE_BUILD_DEBUG + UE_BUILD_DEVELOPMENT + UE_BUILD_TEST + UE_BUILD_SHIPPING >= 1
		PacketHeader* header = reinterpret_cast<PacketHeader*>(sendBuffer->Buffer().GetData());
#else
		PacketHeader* header = reinterpret_cast<PacketHeader*>(sendBuffer->Buffer().data());
#endif
		header->size = packetSize;
		header->id = pktId;
		pkt.SerializeToArray(&header[1], dataSize);
		sendBuffer->Close(packetSize);

		return sendBuffer;
	}
};