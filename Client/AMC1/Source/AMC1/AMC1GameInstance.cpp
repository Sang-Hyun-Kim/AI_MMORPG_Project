#include "AMC1GameInstance.h"
#include "Network/NetworkWorker.h"
#include "Network/ClientPacketHandler.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "Engine/Engine.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

void UAMC1GameInstance::Init()
{
	Super::Init();

	// 언리얼 내장 데디케이티드 서버 인스턴스는 C++ 소켓 접속 제외 (중복 유령 세션 방지)
	if (IsRunningDedicatedServer())
		return;

	ClientPacketHandler::Init();
	ClientPacketHandler::GGameInstance = this;

	ConnectToServer();
}

void UAMC1GameInstance::Shutdown()
{
	DisconnectFromServer();

	// 댕글링 포인터 방지를 위한 전역 약참조 초기화
	ClientPacketHandler::GGameInstance = nullptr;

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
}

void UAMC1GameInstance::ConnectToServer()
{
	// [Item 1] 하드코딩 제거: 커맨드라인 > Config(.ini) > UPROPERTY 기본값 순으로 주소를 확정합니다.
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

		// 임시 C_LOGIN 전송 테스트 (멀티클라이언트 테스트를 위해 랜덤 티켓 발급)
		int32 RandomSuffix = FMath::RandRange(1, 100);
		FString TicketStr = FString::Printf(TEXT("DummyTicket_%d"), RandomSuffix);
		Protocol::C_LOGIN LoginPkt;
		LoginPkt.set_ticket(TCHAR_TO_UTF8(*TicketStr)); 

		SendBufferRef SendBuf = ClientPacketHandler::MakeSendBuffer(LoginPkt);
		if (SendBuf.IsValid())
		{
			int32 BytesSent = 0;
			Socket->Send(SendBuf->Buffer().GetData(), SendBuf->Buffer().Num(), BytesSent);
			UE_LOG(LogTemp, Log, TEXT("[UAMC1GameInstance] C_LOGIN Sent: %d bytes"), BytesSent);
			if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, FString::Printf(TEXT("[Network] C_LOGIN Sent: %d bytes"), BytesSent));
		}
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
