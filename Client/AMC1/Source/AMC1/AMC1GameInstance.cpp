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

	ClientPacketHandler::Init();

	ConnectToServer();
}

void UAMC1GameInstance::Shutdown()
{
	DisconnectFromServer();

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

		// 수신 스레드 구동
		NetworkWorker = MakeShared<FNetworkWorker>(Socket);

		// 임시 C_LOGIN 전송 테스트
		Protocol::C_LOGIN LoginPkt;
		LoginPkt.set_ticket("DummyTicket"); // 더미 티켓

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
