#pragma once

#include "CoreMinimal.h"
#include "../AMC1.h"   // PacketSession, PacketSessionRef, SendBufferRef

class UAMC1GameInstance;

/*
 * FClientPacketSession — 수신 패킷의 "주인"을 실어 나르는 세션
 * ────────────────────────────────────────────────────────────────────────────
 * [왜 만들었나 — 결함 UE-1 / 2026-09-04]
 *
 *   기존에는 수신 핸들러가 목적지 GameInstance를 전역에서 찾았습니다.
 *
 *       static TWeakObjectPtr<UAMC1GameInstance> ClientPacketHandler::GGameInstance;
 *
 *   그런데 이것은 전역 레지스트리로 설계된 것이 아니라,
 *   FNetworkWorker::Run()의 AsyncTask 람다 안에서 **핸들러에게 값을 넘길 방법이 없어
 *   쓴 우회로**였습니다. 실제로 디스패치 직전에 대입하고 직후에 nullptr로 지웁니다.
 *
 *       ClientPacketHandler::GGameInstance = GI;         // 넣고
 *       PacketSessionRef DummySession = nullptr;         // 진짜 넘길 자리는 비운 채
 *       ClientPacketHandler::HandlePacket(DummySession, Span);
 *       ClientPacketHandler::GGameInstance = nullptr;    // 지운다
 *
 *   PacketSession 추상 클래스(AMC1.h:32)는 이미 있었지만 구현체가 없어
 *   nullptr이 전달되고 있었습니다. 이 클래스가 그 빈자리를 채웁니다.
 *
 * [무엇이 고쳐지는가]
 *
 *   언리얼은 **클라이언트 프로그램 하나당 GameInstance 하나**를 전제합니다.
 *   그래서 패키징된 빌드에서는 전역이 "우연히" 동작했습니다.
 *   그러나 단일 프로세스 PIE에서는 창마다 UAMC1GameInstance가 따로 만들어지고
 *   **셋 다 이 하나뿐인 static 슬롯에 자기 자신을 덮어씁니다.**
 *   그 결과 세 소켓에서 온 패킷이 전부 마지막 GameInstance의 월드로 흘러가,
 *   "한 창만 움직여도 다른 창이 반응하는" 증상이 났습니다. (2026-09-04 사용자 보고)
 *
 *   소유자를 인자로 넘기면 각 연결이 자기 월드로만 흘러갑니다.
 *
 * [불변식]
 *
 *   1. 클라이언트에서 만들어지는 PacketSession은 **항상 이 타입**입니다.
 *      그래서 GetGameInstanceFrom()의 static_cast가 안전합니다.
 *      (언리얼은 기본적으로 RTTI가 꺼져 있어 dynamic_cast를 쓸 수 없습니다.)
 *      다른 파생 세션을 추가한다면 그 캐스팅을 먼저 손봐야 합니다.
 *
 *   2. 소유자는 **약참조**로 들고 있습니다. 세션은 워커 스레드에서 온 패킷을
 *      게임 스레드에서 처리하는 동안 살아 있어야 하는데, 그 사이에 GameInstance가
 *      파괴될 수 있기 때문입니다(PIE 종료 등). 강참조로 바꾸면 GC를 방해하고,
 *      raw 포인터로 바꾸면 댕글링이 됩니다.
 *
 *   3. 이 파일은 **패킷 제너레이터가 건드리지 않는 자리**에 있습니다.
 *      GenPackets.bat은 ClientPacketHandler.h를 통째로 덮어쓰므로,
 *      이 클래스를 그 헤더로 옮기면 다음 제너레이터 실행 때 사라집니다.
 *      ⚠️ 여기서 ClientPacketHandler.h로 이사시키지 마십시오.
 */
class AMC1_API FClientPacketSession : public PacketSession
{
public:
	explicit FClientPacketSession(UAMC1GameInstance* InGameInstance);
	virtual ~FClientPacketSession() override = default;

	/** 이 연결의 소유 GameInstance를 통해 송신합니다. 소유자가 사라졌으면 조용히 무시합니다. */
	virtual void Send(SendBufferRef sendBuffer) override;

	/** 소유 GameInstance. 이미 파괴되었으면 nullptr. */
	UAMC1GameInstance* GetGameInstance() const;

private:
	TWeakObjectPtr<UAMC1GameInstance> WeakGameInstance;
};

/**
 * 수신 핸들러에서 소유 GameInstance를 꺼내는 헬퍼.
 * 세션이 없거나 소유자가 파괴되었으면 nullptr을 돌려주므로 호출부는 항상 검사해야 합니다.
 */
AMC1_API UAMC1GameInstance* GetGameInstanceFrom(const PacketSessionRef& Session);
