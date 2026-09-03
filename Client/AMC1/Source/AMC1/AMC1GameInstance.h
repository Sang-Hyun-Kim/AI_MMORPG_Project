#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "TimerManager.h"
#include "AMC1GameInstance.generated.h"

class FSocket;
class FNetworkWorker;

/*
 * UAMC1GameInstance
 * 역할: 클라이언트 프로세스 수명 동안 유지되는 네트워크 진입점.
 *       게임 서버 소켓 연결/해제, 수신 워커 스레드 소유, Heartbeat PING 타이머를 관리합니다.
 *
 * [Phase 4 Item 1] 서버 주소 외부화 (2026-09-03)
 *   과거에는 ConnectToServer() 안에 "127.0.0.1" / 7777 이 하드코딩되어 있어
 *   AWS EC2의 Elastic IP로 접속하려면 코드를 고치고 다시 빌드해야 했습니다.
 *   이제 아래 우선순위로 주소를 결정합니다:
 *     1순위. 커맨드라인 인수  -ServerIP=1.2.3.4 -ServerPort=7777
 *     2순위. Config(.ini) 값  [/Script/AMC1.AMC1GameInstance] 섹션
 *     3순위. UPROPERTY 기본값 (에디터 디테일 패널에서 직접 편집 가능)
 *   UCLASS에 Config=Game 을 지정해야 Config 지정자가 동작하므로 함께 변경했습니다.
 */
UCLASS(Config=Game)
class AMC1_API UAMC1GameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	virtual void Init() override;
	virtual void Shutdown() override;

	void ConnectToServer();
	void DisconnectFromServer();

public:
	/** 접속할 게임 서버 IPv4 주소(점 표기). AWS 배포 시 Elastic IP를 넣습니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "Network")
	FString ServerIP = TEXT("127.0.0.1");

	/** 접속할 게임 서버 포트. 서버 Config.json의 Server.Port와 일치해야 합니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "Network")
	int32 ServerPort = 7777;

public:
	// ⚠️ SendPacket / SendPing 은 반드시 public 이어야 합니다.
	//    외부 호출부: AMC1PlayerController.cpp, Network/ClientPacketHandler.cpp
	//    (2026-09-03 회귀 사고: ResolveServerAddress()를 추가하며 private: 블록을 위에 삽입해
	//     이 두 함수가 private으로 딸려 들어가 C2248 컴파일 에러가 발생했음)
	void SendPacket(TSharedPtr<class SendBuffer> SendBuf);
	void SendPing();

private:
	/** 커맨드라인 인수가 있으면 ServerIP/ServerPort를 덮어씁니다. ConnectToServer() 직전에 호출됩니다. */
	void ResolveServerAddress();

private:
	FSocket* Socket;
	TSharedPtr<FNetworkWorker> NetworkWorker;
	FTimerHandle PingTimerHandle;
};
