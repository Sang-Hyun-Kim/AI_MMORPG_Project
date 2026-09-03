#pragma once


#include "Protocol.pb.h"
#include "CorePch.h"
#include "Session.h"
#include "SendBuffer.h"


#include <array>
#include <span>
#include <functional>



// C++20: std::span을 활용하여 버퍼 오버플로우를 방지하는 모던 핸들러 시그니처
using PacketHandlerFunc = std::function<bool(PacketSessionRef&, std::span<std::byte>)>;
// [S2] 배열 크기는 UINT16_MAX(=65535)가 아니라 UINT16_MAX + 1(=65536)이어야 합니다.
// header.id는 uint16이라 65535까지 가질 수 있는데 크기가 65535면 유효 인덱스는 65534까지이므로,
// id == 65535인 패킷 하나만으로 배열 범위 밖의 std::function을 호출하게 됩니다(미정의 동작).
extern std::array<PacketHandlerFunc, UINT16_MAX + 1> GPacketHandler;

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
bool Handle_C_LOGIN(PacketSessionRef& session, Protocol::C_LOGIN& pkt);
bool Handle_C_ENTER_GAME(PacketSessionRef& session, Protocol::C_ENTER_GAME& pkt);
bool Handle_C_LEAVE_GAME(PacketSessionRef& session, Protocol::C_LEAVE_GAME& pkt);
bool Handle_C_MOVE(PacketSessionRef& session, Protocol::C_MOVE& pkt);
bool Handle_C_CHAT(PacketSessionRef& session, Protocol::C_CHAT& pkt);
bool Handle_C_PING(PacketSessionRef& session, Protocol::C_PING& pkt);
bool Handle_C_ATTACK(PacketSessionRef& session, Protocol::C_ATTACK& pkt);
bool Handle_C_CHECK_MAILBOX(PacketSessionRef& session, Protocol::C_CHECK_MAILBOX& pkt);

class ServerPacketHandler
{
public:
	static void Init()
	{
		// [S2] 마지막 칸(인덱스 65535)까지 빠짐없이 초기화합니다.
		for (int32 i = 0; i <= UINT16_MAX; i++)
			GPacketHandler[i] = Handle_INVALID;
		GPacketHandler[static_cast<uint16>(PacketID::PKT_C_LOGIN)] = [](PacketSessionRef& session, std::span<std::byte> buffer) { return HandlePacket<Protocol::C_LOGIN>(Handle_C_LOGIN, session, buffer); };
		GPacketHandler[static_cast<uint16>(PacketID::PKT_C_ENTER_GAME)] = [](PacketSessionRef& session, std::span<std::byte> buffer) { return HandlePacket<Protocol::C_ENTER_GAME>(Handle_C_ENTER_GAME, session, buffer); };
		GPacketHandler[static_cast<uint16>(PacketID::PKT_C_LEAVE_GAME)] = [](PacketSessionRef& session, std::span<std::byte> buffer) { return HandlePacket<Protocol::C_LEAVE_GAME>(Handle_C_LEAVE_GAME, session, buffer); };
		GPacketHandler[static_cast<uint16>(PacketID::PKT_C_MOVE)] = [](PacketSessionRef& session, std::span<std::byte> buffer) { return HandlePacket<Protocol::C_MOVE>(Handle_C_MOVE, session, buffer); };
		GPacketHandler[static_cast<uint16>(PacketID::PKT_C_CHAT)] = [](PacketSessionRef& session, std::span<std::byte> buffer) { return HandlePacket<Protocol::C_CHAT>(Handle_C_CHAT, session, buffer); };
		GPacketHandler[static_cast<uint16>(PacketID::PKT_C_PING)] = [](PacketSessionRef& session, std::span<std::byte> buffer) { return HandlePacket<Protocol::C_PING>(Handle_C_PING, session, buffer); };
		GPacketHandler[static_cast<uint16>(PacketID::PKT_C_ATTACK)] = [](PacketSessionRef& session, std::span<std::byte> buffer) { return HandlePacket<Protocol::C_ATTACK>(Handle_C_ATTACK, session, buffer); };
		GPacketHandler[static_cast<uint16>(PacketID::PKT_C_CHECK_MAILBOX)] = [](PacketSessionRef& session, std::span<std::byte> buffer) { return HandlePacket<Protocol::C_CHECK_MAILBOX>(Handle_C_CHECK_MAILBOX, session, buffer); };
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
	static SendBufferRef MakeSendBuffer(Protocol::S_ATTACK& pkt) { return MakeSendBuffer(pkt, static_cast<uint16>(PacketID::PKT_S_ATTACK)); }
	static SendBufferRef MakeSendBuffer(Protocol::S_STATUS_CHANGE& pkt) { return MakeSendBuffer(pkt, static_cast<uint16>(PacketID::PKT_S_STATUS_CHANGE)); }

private:
	template<typename PacketType, typename ProcessFunc>
	static bool HandlePacket(ProcessFunc func, PacketSessionRef& session, std::span<std::byte> buffer)
	{
		PacketType pkt;

		// [S3] 길이 계산 언더플로우 방어.
		// sizeof()는 size_t(부호 없음)이므로 int32와 섞어 빼면 int32가 size_t로 승격되어,
		// buffer.size()가 4보다 작을 때 뺄셈이 언더플로우하여 거대한 값이 됩니다.
		// 그 값이 int 매개변수로 좁혀지면서 비정상 길이가 Protobuf에 전달되어 버퍼 밖을 읽습니다.
		// PacketSession::OnRecv의 [S1] 하한 검증이 들어가면 여기까지 오지 않지만,
		// 파싱 진입점 자체에도 방어를 두어 이중으로 막습니다.
		const size_t bufferSize = buffer.size();
		if (bufferSize < sizeof(PacketHeader))
			return false;

		const int32 payloadSize = static_cast<int32>(bufferSize - sizeof(PacketHeader));
		if (pkt.ParseFromArray(buffer.data() + sizeof(PacketHeader), payloadSize) == false)
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