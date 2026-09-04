#include "AMC1GameInstance.h"
#include "Blueprint/UserWidget.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "UObject/UObjectGlobals.h"   // FCoreUObjectDelegates::PostLoadMapWithWorld
#include "Manager/AMC1ObjectManager.h"
#include "Network/ClientPacketHandler.h"
#include "Network/NetworkWorker.h"
#include "Network/ClientPacketHandler.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "Engine/Engine.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
// [2026-09-04] C# 인증 백엔드 연동용. AMC1.Build.cs에 "HTTP", "Json" 모듈이 필요합니다.
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

void UAMC1GameInstance::Init()
{
	Super::Init();

	// 언리얼 내장 데디케이티드 서버 인스턴스는 C++ 소켓 접속 제외 (중복 유령 세션 방지)
	if (IsRunningDedicatedServer())
		return;

	ClientPacketHandler::Init();
	// [2026-09-04] ClientPacketHandler::GGameInstance = this;  <- 제거됨 (결함 UE-1)
	//   소유자는 이제 수신 시점에 FClientPacketSession으로 전달됩니다.
	//   전역에 자기 자신을 등록하면 PIE 다중 창에서 서로를 덮어씁니다.

	/*
	 * [2026-09-04 / T4] 레벨 로드 완료 알림 구독
	 *
	 *   두 가지 목적을 겸합니다.
	 *     · 로그인 레벨이 뜨면 → 로그인 위젯을 올린다
	 *     · 게임 레벨이 뜨면   → ObjectManager를 정리하고 C_ENTER_GAME을 보낸다
	 *
	 *   ⚠️ Shutdown()에서 반드시 해제해야 합니다. 남겨두면 GameInstance가 파괴된 뒤
	 *      호출되어 크래시합니다.
	 */
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(
		this, &UAMC1GameInstance::HandlePostLoadMap);

	// 커맨드라인/ini 값을 먼저 확정합니다(위젯 기본값 표시에도 쓰입니다).
	ResolveServerAddress();

	/*
	 * [2026-09-04 / T2·T3] 자동 로그인은 "위젯이 없을 때만" 수행합니다.
	 *
	 *   변경 전: 여기서 무조건 LoginAndConnect()를 불렀습니다. 그래서 PIE를 누르면
	 *     알아서 접속했고, 로그인 화면이 낄 자리가 없었습니다.
	 *
	 *   변경 후: LoginWidgetClass가 지정되어 있으면 사용자가 Submit할 때까지 기다립니다.
	 *
	 *   ⚠️ **미지정 시 자동 로그인을 유지하는 이 분기를 지우지 마십시오.**
	 *      WBP 에셋이 아직 없는 상태에서 자동 로그인을 떼면 게임에 들어갈 방법이
	 *      사라집니다. 위젯이 준비될 때까지의 안전망입니다.
	 */
	if (LoginWidgetClass.IsNull())
	{
		UE_LOG(LogTemp, Log,
			TEXT("[UAMC1GameInstance] LoginWidgetClass is not set - falling back to auto login."));
		LoginAndConnect();
	}
	else
	{
		UE_LOG(LogTemp, Log,
			TEXT("[UAMC1GameInstance] Waiting for login widget submit."));
	}
}

void UAMC1GameInstance::Shutdown()
{
	DisconnectFromServer();

	// [2026-09-04] 전역 약참조 초기화 제거 (결함 UE-1)
	//   전역이 없어졌으므로 정리할 대상도 없습니다. 세션이 들고 있는 약참조는
	//   GameInstance가 파괴되면 스스로 무효화됩니다.

	// [2026-09-04 / T4] 레벨 로드 구독 해제. 남기면 파괴된 객체로 호출되어 크래시합니다.
	if (PostLoadMapHandle.IsValid())
	{
		FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
		PostLoadMapHandle.Reset();
	}

	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(EnterGameRetryTimer);
		GetWorld()->GetTimerManager().ClearTimer(LoginWidgetRetryTimer);
	}

	Super::Shutdown();
}

/*
 * ResolveServerAddress
 * 커맨드라인 인수를 확인해 ServerIP / ServerPort 를 덮어씁니다.
 * 인수가 없으면 Config(.ini) 또는 UPROPERTY 기본값이 그대로 유지됩니다.
 *   예) AMC1.exe -ServerIP=13.125.10.20 -ServerPort=7777
 * 에디터 PIE에서는 커맨드라인을 넣기 번거로우므로, 디테일 패널의 UPROPERTY 편집이 주 경로입니다.
 */
void UAMC1GameInstance::ResolveServerAddress()
{
	FString CmdIP;
	if (FParse::Value(FCommandLine::Get(), TEXT("ServerIP="), CmdIP) && !CmdIP.IsEmpty())
	{
		ServerIP = CmdIP;
		UE_LOG(LogTemp, Log, TEXT("[UAMC1GameInstance] ServerIP overridden by command line: %s"), *ServerIP);
	}

	int32 CmdPort = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("ServerPort="), CmdPort))
	{
		// 포트 유효 범위를 검사합니다. 잘못된 값이면 무시하고 기존 값을 유지합니다.
		if (CmdPort > 0 && CmdPort <= 65535)
		{
			ServerPort = CmdPort;
			UE_LOG(LogTemp, Log, TEXT("[UAMC1GameInstance] ServerPort overridden by command line: %d"), ServerPort);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[UAMC1GameInstance] Ignored invalid -ServerPort=%d (must be 1..65535)"), CmdPort);
		}
	}

	// [2026-09-04] 인증 관련 설정도 같은 규약(커맨드라인 > .ini > 기본값)으로 해석합니다.
	//   예) AMC1.exe -AuthBaseUrl=http://13.125.10.20:5000 -AccountName=test02 -Password=test02
	FString CmdAuthUrl;
	if (FParse::Value(FCommandLine::Get(), TEXT("AuthBaseUrl="), CmdAuthUrl) && !CmdAuthUrl.IsEmpty())
	{
		AuthBaseUrl = CmdAuthUrl;
		UE_LOG(LogTemp, Log, TEXT("[UAMC1GameInstance] AuthBaseUrl overridden by command line: %s"), *AuthBaseUrl);
	}

	FString CmdAccount;
	if (FParse::Value(FCommandLine::Get(), TEXT("AccountName="), CmdAccount) && !CmdAccount.IsEmpty())
	{
		AccountName = CmdAccount;
		UE_LOG(LogTemp, Log, TEXT("[UAMC1GameInstance] AccountName overridden by command line: %s"), *AccountName);
	}

	FString CmdPassword;
	if (FParse::Value(FCommandLine::Get(), TEXT("Password="), CmdPassword) && !CmdPassword.IsEmpty())
	{
		// 비밀번호 값 자체는 로그에 남기지 않습니다.
		Password = CmdPassword;
		UE_LOG(LogTemp, Log, TEXT("[UAMC1GameInstance] Password overridden by command line."));
	}
}

/*
 * LoginAndConnect
 * [2026-09-04 신설] 로그인 → 접속 순서를 강제하는 진입점입니다.
 *
 * HTTP 요청은 비동기이므로, 응답 콜백(OnLoginResponse) 안에서만 게임 서버에 접속합니다.
 * 티켓 없이 먼저 접속하면 서버가 C_LOGIN을 검증하지 못해 즉시 강퇴되므로,
 * "접속은 티켓을 받은 뒤에만"이라는 순서를 코드 구조로 보장합니다.
 */
void UAMC1GameInstance::LoginAndConnect()
{
	// 커맨드라인 > .ini > UPROPERTY 기본값 순으로 주소·계정을 확정합니다.
	ResolveServerAddress();

	if (bBypassAuthServer)
	{
		/*
		 * 비상 폴백: C# 백엔드를 건너뛰고 테스트 백도어 티켓으로 바로 접속합니다.
		 * 서버의 Handle_C_LOGIN은 "dummy" 접두사 티켓의 끝 숫자를 PlayerId로 해석하므로
		 * dummy_<BypassPlayerId> 형태를 보내면 신원이 결정적으로 유지됩니다.
		 * (데모 당일 C#이 기동하지 못했을 때를 위한 경로입니다.)
		 */
		const FString BackdoorTicket = FString::Printf(TEXT("dummy_%d"), BypassPlayerId);
		ExpectedPlayerId = BypassPlayerId;

		UE_LOG(LogTemp, Warning, TEXT("[UAMC1GameInstance] Auth bypass enabled. Using backdoor ticket for PlayerId %d."), BypassPlayerId);
		if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 8.f, FColor::Yellow,
			FString::Printf(TEXT("[Auth] BYPASS mode - PlayerId %d"), BypassPlayerId));

		PendingTicket = BackdoorTicket;
		ConnectToServer();
		return;
	}

	RequestLoginTicket();
}

/*
 * RequestLoginTicket
 * C# 백엔드에 POST /api/auth/login 을 보냅니다.
 * 성공하면 백엔드가 Redis에 Ticket:User:<ticket> -> PlayerId 를 저장하고
 * 응답 본문으로 ticket 과 playerId 를 돌려줍니다.
 */
void UAMC1GameInstance::RequestLoginTicket()
{
	const FString Url = AuthBaseUrl + TEXT("/api/auth/login");

	// 계정명/비밀번호에 따옴표나 역슬래시가 들어가도 깨지지 않도록 JSON 직렬화기를 씁니다.
	// (문자열을 직접 이어붙이면 이스케이프 처리를 빠뜨리기 쉽습니다.)
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("AccountName"), AccountName);
	Body->SetStringField(TEXT("Password"), Password);

	FString Payload;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Payload);
	FJsonSerializer::Serialize(Body.ToSharedRef(), Writer);

	// 반환 타입(FHttpRequestRef)은 엔진 버전에 따라 별칭이 달라질 수 있어 auto로 받습니다.
	auto Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Url);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetContentAsString(Payload);
	// 데모 현장에서 무한 대기하지 않도록 타임아웃을 둡니다.
	Request->SetTimeout(10.0f);
	Request->OnProcessRequestComplete().BindUObject(this, &UAMC1GameInstance::OnLoginResponse);

	UE_LOG(LogTemp, Log, TEXT("[UAMC1GameInstance] Requesting login ticket: %s (Account: %s)"), *Url, *AccountName);
	if (GEngine) GEngine->AddOnScreenDebugMessage(2, 8.f, FColor::Cyan,
		FString::Printf(TEXT("[Auth] Logging in as %s ..."), *AccountName));

	Request->ProcessRequest();
}

/*
 * OnLoginResponse
 * 로그인 HTTP 응답 처리. 이 콜백은 게임 스레드에서 호출되므로 별도 디스패치가 필요 없습니다.
 *
 * 실패 시 접속하지 않고 화면에 사유를 남깁니다.
 * 조용히 실패해서 "왜 접속이 안 되지"로 시간을 쓰는 상황을 만들지 않기 위함입니다.
 */
void UAMC1GameInstance::OnLoginResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
{
	auto Fail = [this](const FString& Reason)
	{
		UE_LOG(LogTemp, Error, TEXT("[UAMC1GameInstance] Login failed: %s"), *Reason);
		if (GEngine) GEngine->AddOnScreenDebugMessage(2, 12.f, FColor::Red,
			FString::Printf(TEXT("[Auth] Login FAILED - %s"), *Reason));
		// [T3] 위젯이 자기 자리에 사유를 표시할 수 있도록 결과를 알립니다.
		OnLoginResult.Broadcast(false, Reason);
	};

	if (!bConnectedSuccessfully || !Response.IsValid())
	{
		// 백엔드 미기동 / 주소 오타 / 방화벽이 모두 이 경로로 들어옵니다.
		Fail(FString::Printf(TEXT("no response from %s (backend down? URL typo? firewall?)"), *AuthBaseUrl));
		return;
	}

	const int32 Code = Response->GetResponseCode();
	if (Code != 200)
	{
		// 307은 HTTPS 리다이렉트를 의미합니다. 백엔드가 운영 환경에서
		// UseHttpsRedirection을 끄도록 수정했으므로, 이 값이 보이면 그 설정이 되돌아간 것입니다.
		Fail(FString::Printf(TEXT("HTTP %d %s"), Code,
			Code == 307 ? TEXT("(HTTPS redirect - check backend UseHttpsRedirection)") : TEXT("")));
		return;
	}

	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
	{
		Fail(TEXT("malformed JSON response"));
		return;
	}

	FString Ticket;
	if (!Json->TryGetStringField(TEXT("ticket"), Ticket) || Ticket.IsEmpty())
	{
		Fail(TEXT("response has no ticket"));
		return;
	}

	// playerId는 없어도 접속에는 지장이 없으므로 실패로 보지 않습니다(대조용 정보).
	// TryGetNumberField의 정수 오버로드는 엔진 버전에 따라 다르므로 double로 받아 캐스팅합니다.
	double PlayerIdNumber = 0.0;
	if (Json->TryGetNumberField(TEXT("playerId"), PlayerIdNumber))
	{
		ExpectedPlayerId = static_cast<int64>(PlayerIdNumber);
	}

	UE_LOG(LogTemp, Log, TEXT("[UAMC1GameInstance] Login success. PlayerId=%lld"), ExpectedPlayerId);
	if (GEngine) GEngine->AddOnScreenDebugMessage(2, 8.f, FColor::Green,
		FString::Printf(TEXT("[Auth] Login OK - PlayerId %lld"), ExpectedPlayerId));

	OnLoginResult.Broadcast(true, FString::Printf(TEXT("PlayerId %lld"), ExpectedPlayerId));

	PendingTicket = Ticket;
	ConnectToServer();
}

/*
 * SubmitLogin — 위젯의 로그인 버튼이 호출합니다. [T3]
 */
void UAMC1GameInstance::SubmitLogin(const FString& InAccountName, const FString& InPassword)
{
	if (InAccountName.IsEmpty() || InPassword.IsEmpty())
	{
		// 서버 왕복 없이 즉시 되돌려 줍니다. 빈 값으로 요청을 보내면 401만 받고
		// 사용자는 "서버 문제"로 오해하게 됩니다.
		OnLoginResult.Broadcast(false, TEXT("계정과 비밀번호를 모두 입력하십시오."));
		return;
	}

	AccountName = InAccountName;
	Password = InPassword;

	UE_LOG(LogTemp, Log, TEXT("[UAMC1GameInstance] SubmitLogin as '%s'"), *AccountName);
	LoginAndConnect();
}

/*
 * NotifyLoginWidgetShown — 로그인 컨트롤러가 위젯을 띄웠음을 알립니다.
 *
 * 이 값이 채워지면 ShowLoginWidget()은 즉시 반환하므로 중복 생성이 없습니다.
 * 또한 OnAuthenticatedEnterWorld()가 레벨 전환 직전에 이 위젯을 내립니다.
 */
void UAMC1GameInstance::NotifyLoginWidgetShown(UUserWidget* InWidget)
{
	LoginWidget = InWidget;

	// 컨트롤러가 책임졌으므로 폴백 재시도 타이머는 더 이상 필요 없습니다.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LoginWidgetRetryTimer);
	}
}

/*
 * OnAuthenticatedEnterWorld — S_LOGIN 성공 시 호출됩니다. [T4]
 *
 * GameLevelName이 비어 있거나 이미 그 레벨에 있으면 **전환하지 않고** 곧바로
 * C_ENTER_GAME을 보냅니다. 즉 레벨을 아직 나누지 않은 현재 상태에서는
 * 기존 동작과 완전히 동일합니다.
 */
void UAMC1GameInstance::OnAuthenticatedEnterWorld()
{
	// 로그인 위젯이 떠 있으면 내립니다(레벨 전환으로도 사라지지만 명시적으로 처리).
	if (LoginWidget)
	{
		LoginWidget->RemoveFromParent();
		LoginWidget = nullptr;
	}

	if (GameLevelName.IsNone())
	{
		SendEnterGame();
		return;
	}

	/*
	 * [2026-09-04 보강] 짧은 이름과 전체 경로를 모두 받아들입니다.
	 *
	 *   GetCurrentLevelName(bRemovePrefixString=true)은 **짧은 이름**을 돌려줍니다("LobbyLevel").
	 *   그런데 설정에는 경로를 적기 쉽습니다("/Game/Level/LobbyLevel").
	 *   그대로 비교하면 항상 불일치로 판정되어, 이미 그 레벨에 있어도 다시 여는
	 *   불필요한 재로드가 발생합니다. 마지막 '/' 뒤만 잘라 비교합니다.
	 *   (레벨을 Level 폴더로 옮기면서 실제로 발생 가능해진 문제입니다.)
	 */
	FString TargetShort = GameLevelName.ToString();
	int32 SlashIdx = INDEX_NONE;
	if (TargetShort.FindLastChar(TEXT('/'), SlashIdx))
	{
		TargetShort = TargetShort.RightChop(SlashIdx + 1);
	}
	// "/Game/Level/LobbyLevel.LobbyLevel" 같은 표기도 처리합니다.
	int32 DotIdx = INDEX_NONE;
	if (TargetShort.FindChar(TEXT('.'), DotIdx))
	{
		TargetShort = TargetShort.Left(DotIdx);
	}

	const FString CurrentLevel = UGameplayStatics::GetCurrentLevelName(this, /*bRemovePrefixString=*/true);
	if (CurrentLevel.Equals(TargetShort, ESearchCase::IgnoreCase))
	{
		// 이미 게임 레벨입니다. 전환하면 방금 만든 월드를 버리게 되므로 하지 않습니다.
		UE_LOG(LogTemp, Log, TEXT("[UAMC1GameInstance] Already in '%s' - skipping level transition."), *CurrentLevel);
		SendEnterGame();
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[UAMC1GameInstance] Auth OK -> OpenLevel(%s)"), *GameLevelName.ToString());
	bPendingEnterGame = true;
	UGameplayStatics::OpenLevel(this, GameLevelName);
}

/*
 * HandlePostLoadMap — 모든 레벨 로드 완료 시 호출됩니다. [T4·T5]
 */
void UAMC1GameInstance::HandlePostLoadMap(UWorld* LoadedWorld)
{
	/*
	 * ⛔ [2026-09-04 / 결함 UE-2] 남의 맵 로드에 반응하지 않도록 걸러냅니다.
	 *
	 * FCoreUObjectDelegates::PostLoadMapWithWorld 는 **프로세스 전역 static 델리게이트**입니다.
	 *     UObjectGlobals.h:3472
	 *     static COREUOBJECT_API FPostLoadMapDelegate PostLoadMapWithWorld;
	 *
	 * 단일 프로세스 PIE에서 창을 여럿 띄우면 각 창의 GameInstance가 **같은 전역
	 * 델리게이트에 함께 구독**됩니다. 그래서 **어느 한 창이 맵을 로드하면 모든
	 * GameInstance의 이 함수가 호출**됩니다. 걸러내지 않으면:
	 *
	 *   · 플레이 중이던 다른 창에 ShowLoginWidget()이 돌아
	 *     **게임 화면 위에 로그인 창이 다시 뜹니다.**
	 *     (2026-09-04 사용자 보고: "2가 로그인하면 1이 로그인 화면으로 전환됨")
	 *   · ObjectManager::ResetForNewLevel()이 돌아
	 *     **남의 프록시 캐릭터가 전부 지워집니다.** → 동기화가 깨진 것처럼 보임
	 *   · bPendingEnterGame이 서 있으면 엉뚱한 인스턴스에서 C_ENTER_GAME이 나갑니다
	 *
	 * 이 한 줄이 그 통로를 막습니다. **절대 지우지 마십시오.**
	 * (패키징된 단일 클라이언트에서는 GameInstance가 하나뿐이라 증상이 드러나지
	 *  않습니다. PIE 다중 창에서만 터지는, 놓치기 쉬운 유형입니다.)
	 */
	if (LoadedWorld == nullptr || LoadedWorld->GetGameInstance() != this)
	{
		return;
	}

	/*
	 * [T5] ObjectManager는 UGameInstanceSubsystem이라 레벨 전환을 **살아남습니다.**
	 *   그런데 그것이 들고 있는 프록시 액터 포인터·보류 좌표·타이머는 레벨과 함께
	 *   죽거나 무효화됩니다. 정리하지 않으면
	 *     · 프록시 맵에 죽은 키가 남아 재입장 시 스폰을 건너뜀 (남이 안 보임)
	 *     · 옛 세션의 보류 좌표가 새 레벨에 적용됨
	 *     · 보류 재시도 타이머가 옛 월드에 묶여 영영 돌지 않음
	 *   증상이 "가끔"만 나타나 추적이 어려운 종류입니다.
	 */
	if (UAMC1ObjectManager* ObjManager = GetSubsystem<UAMC1ObjectManager>())
	{
		ObjManager->ResetForNewLevel();
	}

	if (bPendingEnterGame)
	{
		// 게임 레벨에 도착했습니다. 폰이 준비되면 C_ENTER_GAME을 보냅니다.
		TrySendEnterGame();
		return;
	}

	// 게임 레벨이 아니면 로그인 화면입니다.
	ShowLoginWidget();
}

/*
 * ShowLoginWidget — LoginWidgetClass가 지정되어 있을 때만 위젯을 올립니다. [T3]
 */
void UAMC1GameInstance::ShowLoginWidget()
{
	if (LoginWidgetClass.IsNull())
	{
		return; // 위젯 미지정 = 자동 로그인 경로. Init()에서 이미 로그인했습니다.
	}

	if (LoginWidget)
	{
		return; // 이미 떠 있음
	}

	UClass* WidgetClass = LoginWidgetClass.LoadSynchronous();
	if (WidgetClass == nullptr)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[UAMC1GameInstance] LoginWidgetClass failed to load: %s"),
			*LoginWidgetClass.ToString());
		return;
	}

	APlayerController* PC = GetFirstLocalPlayerController();
	if (PC == nullptr)
	{
		/*
		 * [2026-09-04 보강] 여기서 포기하면 로그인 화면이 영영 뜨지 않습니다.
		 *
		 *   PostLoadMapWithWorld는 월드 로드 완료 시점이라 PlayerController가
		 *   아직 만들어지지 않았을 수 있습니다. 초판은 경고만 남기고 return 했는데,
		 *   그러면 **위젯이 없으니 Submit도 없고, 자동 로그인도 꺼져 있어
		 *   게임에 들어갈 방법이 완전히 사라집니다.** 준비될 때까지 재시도합니다.
		 */
		UE_LOG(LogTemp, Verbose, TEXT("[UAMC1GameInstance] PlayerController not ready - retrying login widget."));
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(
				LoginWidgetRetryTimer, this, &UAMC1GameInstance::ShowLoginWidget, 0.05f, /*bLoop=*/false);
		}
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LoginWidgetRetryTimer);
	}

	LoginWidget = CreateWidget<UUserWidget>(PC, WidgetClass);
	if (LoginWidget)
	{
		LoginWidget->AddToViewport();
		PC->SetShowMouseCursor(true);
		PC->SetInputMode(FInputModeUIOnly());
	}
}

/*
 * TrySendEnterGame — 폰이 준비되었는지 확인하고 보냅니다. [T4]
 *
 * ⚠️ "레벨 로드 완료"와 "폰 Possess 완료"는 **다른 시점**입니다.
 *   PostLoadMapWithWorld 시점에 GetPawn()이 아직 nullptr일 수 있습니다.
 *   여기서 확인을 생략하면 F10의 타이밍 경합이 형태만 바꿔 되살아납니다.
 */
void UAMC1GameInstance::TrySendEnterGame()
{
	APlayerController* PC = GetFirstLocalPlayerController();
	if (PC != nullptr && PC->GetPawn() != nullptr)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(EnterGameRetryTimer);
		}
		bPendingEnterGame = false;
		SendEnterGame();
		return;
	}

	// 아직입니다. 다음 틱에 다시 봅니다.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			EnterGameRetryTimer, this, &UAMC1GameInstance::TrySendEnterGame, 0.05f, /*bLoop=*/false);
	}
}

/*
 * SendEnterGame — C_ENTER_GAME 송신. [T4]
 *
 * 변경 전에는 이 코드가 ClientPacketHandler의 Handle_S_LOGIN 안에 있었습니다.
 * 레벨 전환을 끼워 넣으려면 "언제 보내는가"를 GameInstance가 통제해야 하므로
 * 이쪽으로 옮겼습니다.
 */
void UAMC1GameInstance::SendEnterGame()
{
	Protocol::C_ENTER_GAME EnterPkt;
	EnterPkt.set_playerindex(0); // 계정당 캐릭터 1개 전제. 캐릭터 선택 도입 시 재검토 [V13]
	SendBufferRef SendBuf = ClientPacketHandler::MakeSendBuffer(EnterPkt);
	SendPacket(SendBuf);

	UE_LOG(LogTemp, Log, TEXT("[UAMC1GameInstance] C_ENTER_GAME sent."));
}

void UAMC1GameInstance::ConnectToServer()
{
	// [Item 1] 하드코딩 제거: 커맨드라인 > Config(.ini) > UPROPERTY 기본값 순으로 주소를 확정합니다.
	// (LoginAndConnect에서 이미 호출되었더라도, 이 함수를 직접 부르는 경로를 위해 유지합니다.
	//  같은 값을 다시 해석할 뿐이라 부작용은 없습니다.)
	ResolveServerAddress();

	Socket = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateSocket(NAME_Stream, TEXT("Default"), false);
	if (Socket == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("[UAMC1GameInstance] CreateSocket failed."));
		if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Red, TEXT("[Network] CreateSocket failed."));
		return;
	}

	FIPv4Address IPAddress;
	if (!FIPv4Address::Parse(ServerIP, IPAddress))
	{
		// 주소 오타는 배포 현장에서 가장 흔한 실패 원인입니다.
		// 조용히 실패하면 원인을 오인하기 쉬우므로 화면과 로그에 명확히 남깁니다.
		UE_LOG(LogTemp, Error, TEXT("[UAMC1GameInstance] Invalid ServerIP '%s'. Expected dotted IPv4 (e.g. 13.125.10.20)."), *ServerIP);
		if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Red,
			FString::Printf(TEXT("[Network] Invalid ServerIP: %s"), *ServerIP));
		return;
	}

	const int32 Port = ServerPort;

	TSharedRef<FInternetAddr> InternetAddr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
	InternetAddr->SetIp(IPAddress.Value);
	InternetAddr->SetPort(Port);

	UE_LOG(LogTemp, Log, TEXT("[UAMC1GameInstance] Try Connecting to %s:%d"), *IPAddress.ToString(), Port);

	bool bConnected = Socket->Connect(*InternetAddr);
	if (bConnected)
	{
		UE_LOG(LogTemp, Log, TEXT("[UAMC1GameInstance] Connected to Server Successfully!"));
		if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, TEXT("[Network] Connected to Server!"));

		// 수신 스레드 구동 (GameInstance 포인터 전달)
		NetworkWorker = MakeShared<FNetworkWorker>(Socket, this);

		// 5초 주기 Heartbeat PING 발송 타이머 등록 (좀비 세션 강퇴 방지)
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(PingTimerHandle, this, &UAMC1GameInstance::SendPing, 5.0f, true);
		}

		/*
		 * [2026-09-04] C_LOGIN 전송 — 난수 더미 티켓 → 로그인으로 받은 실제 티켓
		 *
		 * 변경 전:
		 *     int32 RandomSuffix = FMath::RandRange(1, 100);
		 *     FString TicketStr = FString::Printf(TEXT("DummyTicket_%d"), RandomSuffix);
		 *
		 *   클라이언트가 티켓을 스스로 만들었고, 서버의 백도어가 그것을 Redis에 넣고
		 *   스스로 검증했습니다. 접속할 때마다 접미사가 달라져 **매번 다른 사람으로
		 *   인식**되었고, 그래서 "이동 → 종료 → 재접속 시 그 자리에서 재개"라는
		 *   왕복을 시험할 수 없었습니다. [F7 1번째 겹]
		 *   또한 C# 백엔드가 데모 경로에 전혀 등장하지 않았습니다.
		 *
		 * 변경 후:
		 *   PendingTicket은 C# /api/auth/login이 발급하고 Redis에 PlayerId와 함께
		 *   저장해 둔 1회용 티켓입니다. 서버는 이 티켓을 GET/DEL하고 그 값으로
		 *   신원을 확정합니다.
		 */
		SendLoginPacket(PendingTicket);
	}
	else
	{
		// 실패 시 "어디로" 접속하려 했는지를 반드시 남깁니다.
		// EC2 배포에서는 보안 그룹 / Windows 방화벽 / 서버 미기동이 모두 같은 증상으로 보이므로,
		// 대상 주소가 로그에 없으면 원인을 오인하기 쉽습니다.
		UE_LOG(LogTemp, Error, TEXT("[UAMC1GameInstance] Failed to connect to %s:%d. Check: server running / AWS security group 7777 / Windows Firewall inbound."), *ServerIP, Port);
		if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Red,
			FString::Printf(TEXT("[Network] Connect FAILED -> %s:%d"), *ServerIP, Port));
	}
}

/*
 * SendLoginPacket
 * C_LOGIN 패킷에 티켓을 실어 전송합니다.
 * 티켓이 비어 있으면 보내지 않습니다 — 서버는 빈 티켓을 검증에 실패시키고 세션을 끊는데,
 * 그 경우 화면에는 "접속은 됐는데 곧 끊김"으로만 보여 원인을 오인하기 쉽습니다.
 */
void UAMC1GameInstance::SendLoginPacket(const FString& Ticket)
{
	if (Ticket.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("[UAMC1GameInstance] C_LOGIN aborted: empty ticket. Login must succeed first."));
		if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Red,
			TEXT("[Network] C_LOGIN aborted - no ticket"));
		return;
	}

	if (Socket == nullptr)
		return;

	Protocol::C_LOGIN LoginPkt;
	LoginPkt.set_ticket(TCHAR_TO_UTF8(*Ticket));

	SendBufferRef SendBuf = ClientPacketHandler::MakeSendBuffer(LoginPkt);
	if (SendBuf.IsValid())
	{
		int32 BytesSent = 0;
		Socket->Send(SendBuf->Buffer().GetData(), SendBuf->Buffer().Num(), BytesSent);
		UE_LOG(LogTemp, Log, TEXT("[UAMC1GameInstance] C_LOGIN Sent: %d bytes (ExpectedPlayerId=%lld)"), BytesSent, ExpectedPlayerId);
		if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green,
			FString::Printf(TEXT("[Network] C_LOGIN Sent: %d bytes"), BytesSent));
	}

	// 1회용 티켓이므로 보낸 뒤 지웁니다. 재사용하면 서버가 이미 DEL한 키를 조회해 실패합니다.
	PendingTicket.Empty();
}

void UAMC1GameInstance::DisconnectFromServer()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PingTimerHandle);
	}

	if (NetworkWorker.IsValid())
	{
		NetworkWorker->Destroy();
		NetworkWorker.Reset();
	}

	if (Socket)
	{
		Socket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
		Socket = nullptr;
		UE_LOG(LogTemp, Log, TEXT("[UAMC1GameInstance] Socket Closed and Destroyed."));
	}
}

void UAMC1GameInstance::SendPacket(TSharedPtr<class SendBuffer> SendBuf)
{
	if (Socket && SendBuf.IsValid())
	{
		int32 BytesSent = 0;
		Socket->Send(SendBuf->Buffer().GetData(), SendBuf->Buffer().Num(), BytesSent);
	}
}

void UAMC1GameInstance::SendPing()
{
	Protocol::C_PING PingPkt;
	SendBufferRef SendBuf = ClientPacketHandler::MakeSendBuffer(PingPkt);
	SendPacket(SendBuf);
}
