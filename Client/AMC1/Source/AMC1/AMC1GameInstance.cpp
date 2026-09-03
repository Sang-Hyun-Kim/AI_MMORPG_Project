#include "AMC1GameInstance.h"
#include "Network/NetworkWorker.h"
#include "Network/ClientPacketHandler.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "Engine/Engine.h"

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

void UAMC1GameInstance::ConnectToServer()
{
	Socket = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateSocket(NAME_Stream, TEXT("Default"), false);

	FIPv4Address IPAddress;
	FIPv4Address::Parse(TEXT("127.0.0.1"), IPAddress);
	int32 Port = 7777;

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
		UE_LOG(LogTemp, Error, TEXT("[UAMC1GameInstance] Failed to connect to Server."));
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
