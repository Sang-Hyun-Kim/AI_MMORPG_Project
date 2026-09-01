#include "AMC1GameInstance.h"
#include "Network/NetworkWorker.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"

void UAMC1GameInstance::Init()
{
	Super::Init();

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

		// 수신 스레드 구동
		NetworkWorker = MakeShared<FNetworkWorker>(Socket);
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
