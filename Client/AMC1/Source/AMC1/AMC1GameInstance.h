#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "TimerManager.h"
// [2026-09-04] FHttpRequestPtr / FHttpResponsePtr 타입 선언.
// 이 헤더를 쓰려면 AMC1.Build.cs의 PublicDependencyModuleNames에 "HTTP"가 있어야 합니다.
#include "Interfaces/IHttpRequest.h"
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

	/*
	 * [Phase 4 P3 / 2026-09-04] C# 인증 백엔드 주소
	 *   ServerIP/ServerPort와 동일한 3단 우선순위(커맨드라인 > .ini > 기본값)로 해석됩니다.
	 *   커맨드라인 예) -AuthBaseUrl=http://13.125.10.20:5000
	 *   ⚠️ https가 아니라 http입니다. EC2에는 TLS 인증서가 없으며, 백엔드도
	 *      운영 환경에서는 HTTPS 리다이렉트를 적용하지 않도록 수정했습니다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "Network")
	FString AuthBaseUrl = TEXT("http://127.0.0.1:5000");

	/*
	 * [Phase 4 P3 / 2026-09-04] 로그인 계정 — 결정적 테스트 신원의 핵심
	 *
	 *   과거에는 접속할 때마다 DummyTicket_<난수>를 스스로 만들었기 때문에
	 *   PIE를 다시 돌릴 때마다 서버가 "새로운 사람"으로 인식했고,
	 *   그래서 "저장했다가 다시 불러온다"는 왕복을 시험할 수 없었습니다. [F7 1번째 겹]
	 *
	 *   이제 실제 계정으로 로그인합니다. PIE에서는 아래 기본값이 그대로 쓰이므로
	 *   **항상 같은 캐릭터로 접속**하고, 2인 동시 접속 시험이 필요할 때만
	 *   커맨드라인으로 갈아끼웁니다.
	 *     예) AMC1.exe -AccountName=test02 -Password=test02
	 *   계정은 Server/seed_accounts.sql로 미리 넣어 둡니다(test01~test05).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "Network")
	FString AccountName = TEXT("test01");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "Network")
	FString Password = TEXT("test01");

	/*
	 * 인증 서버 없이 접속할지 여부(비상 폴백).
	 *   true  : C# 로그인을 건너뛰고 테스트 백도어 티켓(dummy_<PlayerId>)으로 바로 접속합니다.
	 *   false : 정상 경로. C# /api/auth/login으로 티켓을 받아 접속합니다.
	 * 데모 당일 C# 백엔드가 기동하지 못했을 때 이 값만 켜면 접속·이동 시연은 살릴 수 있습니다.
	 * (단, 이 경로에서는 계정 검증이 없으므로 "풀스택 로그인" 주장은 할 수 없습니다.)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "Network")
	bool bBypassAuthServer = false;

	/** bBypassAuthServer가 true일 때 사용할 PlayerId. 백도어 티켓 dummy_<이 값>으로 접속합니다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "Network")
	int32 BypassPlayerId = 1;

public:
	// ⚠️ SendPacket / SendPing 은 반드시 public 이어야 합니다.
	//    외부 호출부: AMC1PlayerController.cpp, Network/ClientPacketHandler.cpp
	//    (2026-09-03 회귀 사고: ResolveServerAddress()를 추가하며 private: 블록을 위에 삽입해
	//     이 두 함수가 private으로 딸려 들어가 C2248 컴파일 에러가 발생했음)
	void SendPacket(TSharedPtr<class SendBuffer> SendBuf);
	void SendPing();

	/*
	 * [2026-09-04] 로그인 → 접속의 진입점. Init()에서 호출합니다.
	 *   C# 백엔드에 로그인 요청을 보내고, **응답으로 티켓을 받은 뒤에야** 게임 서버에 접속합니다.
	 *   HTTP는 비동기이므로 순서를 콜백으로 묶습니다. 티켓 없이 먼저 접속하면
	 *   서버가 C_LOGIN을 검증하지 못해 즉시 강퇴됩니다.
	 */
	void LoginAndConnect();

private:
	/*
	 * 커맨드라인 인수가 있으면 네트워크 설정을 덮어씁니다. 접속 직전에 호출됩니다.
	 * 대상: ServerIP, ServerPort, AuthBaseUrl, AccountName, Password
	 * (이름은 하위 호환을 위해 유지합니다. 실제로는 주소 외에 인증 정보도 해석합니다.)
	 */
	void ResolveServerAddress();

	/** C# 백엔드에 POST /api/auth/login 요청을 보냅니다. 응답은 OnLoginResponse에서 처리합니다. */
	void RequestLoginTicket();

	/** 로그인 HTTP 응답 처리. 성공 시 티켓을 담아 ConnectToServer()를 호출합니다. */
	void OnLoginResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully);

	/** 접속 후 C_LOGIN 패킷을 보냅니다. 티켓 문자열을 인자로 받습니다. */
	void SendLoginPacket(const FString& Ticket);

private:
	FSocket* Socket;
	TSharedPtr<FNetworkWorker> NetworkWorker;
	FTimerHandle PingTimerHandle;

	/*
	 * C# 로그인으로 받은 1회용 티켓. ConnectToServer()가 C_LOGIN에 실어 보냅니다.
	 * 서버가 Redis에서 이 티켓을 GET/DEL하고 그 값(PlayerId)으로 신원을 확정합니다.
	 */
	FString PendingTicket;

	/** 서버가 알려준 내 PlayerId (로그인 응답 기준). S_ENTER_GAME 값과 대조해 불일치를 조기에 발견합니다. */
	int64 ExpectedPlayerId = 0;
};
