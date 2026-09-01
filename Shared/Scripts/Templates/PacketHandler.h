#pragma once

{% if ue_project %}
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
{% else %}
#include "Protocol.pb.h"
#include "CorePch.h"
#include "Session.h"
#include "SendBuffer.h"
{% endif %}

#include <array>
#include <span>
#include <functional>

{% if ue_project %}
#if UE_BUILD_DEBUG + UE_BUILD_DEVELOPMENT + UE_BUILD_TEST + UE_BUILD_SHIPPING >= 1
#include "../{{ue_project}}.h"
#endif
{% endif %}

// C++20: std::span을 활용하여 버퍼 오버플로우를 방지하는 모던 핸들러 시그니처
using PacketHandlerFunc = std::function<bool(PacketSessionRef&, std::span<std::byte>)>;
extern std::array<PacketHandlerFunc, UINT16_MAX> GPacketHandler;

// C++20: enum class를 통한 강력한 타입 체크
enum class PacketID : uint16
{
{%- for pkt in parser.total_pkt %}
	PKT_{{pkt.name}} = {{pkt.id}},
{%- endfor %}
};

// Custom Handlers
bool Handle_INVALID(PacketSessionRef& session, std::span<std::byte> buffer);

{%- for pkt in parser.recv_pkt %}
bool Handle_{{pkt.name}}(PacketSessionRef& session, Protocol::{{pkt.name}}& pkt);
{%- endfor %}

class {{output}}
{
public:
	static void Init()
	{
		for (int32 i = 0; i < UINT16_MAX; i++)
			GPacketHandler[i] = Handle_INVALID;

{%- for pkt in parser.recv_pkt %}
		GPacketHandler[static_cast<uint16>(PacketID::PKT_{{pkt.name}})] = [](PacketSessionRef& session, std::span<std::byte> buffer) { return HandlePacket<Protocol::{{pkt.name}}>(Handle_{{pkt.name}}, session, buffer); };
{%- endfor %}
	}

	static bool HandlePacket(PacketSessionRef& session, std::span<std::byte> buffer)
	{
		PacketHeader* header = reinterpret_cast<PacketHeader*>(buffer.data());
		return GPacketHandler[header->id](session, buffer);
	}

{%- for pkt in parser.send_pkt %}
	static SendBufferRef MakeSendBuffer(Protocol::{{pkt.name}}& pkt) { return MakeSendBuffer(pkt, static_cast<uint16>(PacketID::PKT_{{pkt.name}})); }
{%- endfor %}

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
