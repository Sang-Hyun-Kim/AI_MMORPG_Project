#include "ClientPacketSession.h"
#include "../AMC1GameInstance.h"

/*
 * TWeakObjectPtr<T>::Get()은 완전한 타입 T를 요구하므로, 이 구현들은 반드시
 * AMC1GameInstance.h를 포함한 .cpp에 있어야 합니다.
 * 헤더에서는 전방 선언(class UAMC1GameInstance;)만 두어 순환 포함을 피합니다.
 */

FClientPacketSession::FClientPacketSession(UAMC1GameInstance* InGameInstance)
	: WeakGameInstance(InGameInstance)
{
}

void FClientPacketSession::Send(SendBufferRef sendBuffer)
{
	if (UAMC1GameInstance* GI = WeakGameInstance.Get())
	{
		GI->SendPacket(sendBuffer);
	}
	// 소유자가 이미 파괴된 경우(PIE 종료 등)에는 조용히 버립니다.
	// 여기서 로그를 남기면 종료 시퀀스마다 경고가 쏟아집니다.
}

UAMC1GameInstance* FClientPacketSession::GetGameInstance() const
{
	return WeakGameInstance.Get();
}

UAMC1GameInstance* GetGameInstanceFrom(const PacketSessionRef& Session)
{
	if (Session.IsValid() == false)
	{
		return nullptr;
	}

	// 불변식 1: 클라이언트의 PacketSession은 항상 FClientPacketSession입니다.
	// (UE는 기본적으로 RTTI가 꺼져 있어 dynamic_cast를 쓸 수 없습니다.)
	return static_cast<FClientPacketSession*>(Session.Get())->GetGameInstance();
}
