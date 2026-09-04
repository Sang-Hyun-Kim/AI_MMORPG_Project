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
// [S2] 배열 크기는 UINT16_MAX(=65535)가 아니라 UINT16_MAX + 1(=65536)이어야 합니다.
//      header.id(uint16)가 65535일 때 GPacketHandler[65535]는 범위 밖 접근이 됩니다.
//      2026-09-03에 생성물 3벌(UE/DummyClient/GameServer)에 손으로 적용했으나
//      본 템플릿이 갱신되지 않아, GenPackets.bat을 돌리면 조용히 회귀하는
//      상태였습니다(2026-09-04 발견). 여기를 되돌리지 마십시오.
extern std::array<PacketHandlerFunc, UINT16_MAX + 1> GPacketHandler;

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

/*
 * [경고 / 2026-09-04] 이 클래스와 이 파일은 GenPackets.bat이 **통째로 덮어씁니다.**
 *
 *   GenPackets.bat은 생성물을 이동/복사합니다.
 *       move ClientPacketHandler.h -> Client/AMC1/Source/AMC1/Network/
 *       copy ClientPacketHandler.h -> Server/DummyClient/Packet/
 *       move ServerPacketHandler.h -> Server/GameServer/Packet/
 *
 *   따라서 생성된 헤더에 손으로 추가한 코드는 다음 실행 때 조용히 사라집니다.
 *   실제로 UE 헤더에 손으로 넣어 둔 GGameInstance 전역과 [S2] 하드닝이
 *   그런 상태였습니다(2026-09-04 발견). [S2]는 이 템플릿에 반영해 복구했고,
 *   GGameInstance는 Network/ClientPacketSession.h 로 옮겨 제거했습니다.
 *
 *   ⚠️ 프로젝트 고유 코드(UE 전용 타입, 전역 상태 등)를 생성 헤더에 넣지 마십시오.
 *      별도 파일에 두거나, 모든 생성물에 필요한 것이라면 이 템플릿을 고치십시오.
 */
class {{output}}
{
public:
	static void Init()
	{
		for (int32 i = 0; i <= UINT16_MAX; i++) // [S2] 마지막 칸(65535)까지 초기화
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
