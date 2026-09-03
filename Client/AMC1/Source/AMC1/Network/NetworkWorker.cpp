#include "NetworkWorker.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "HAL/RunnableThread.h"
#include "ClientPacketHandler.h"
#include "../AMC1.h"
#include "../AMC1GameInstance.h"
#include "Engine/Engine.h"
#include "Async/Async.h"

FNetworkWorker::FNetworkWorker(FSocket* InSocket, UAMC1GameInstance* InGameInstance)
	: Socket(InSocket), bRunning(false), Thread(nullptr), WeakGameInstance(InGameInstance)
{
	RecvBuffer.SetNumUninitialized(4096); // 4KB 버퍼 초기화
	Thread = FRunnableThread::Create(this, TEXT("NetworkWorkerThread"));
}

FNetworkWorker::~FNetworkWorker()
{
	Destroy();
}

bool FNetworkWorker::Init()
{
	bRunning = true;
	UE_LOG(LogTemp, Log, TEXT("[FNetworkWorker] Thread Initialized"));
	return true;
}

uint32 FNetworkWorker::Run()
{
	while (bRunning)
	{
		if (Socket == nullptr || Socket->GetConnectionState() != SCS_Connected)
		{
			break;
		}

		uint32 Size = 0;
		if (Socket->HasPendingData(Size))
		{
			int32 ReadBytes = 0;
			bool bRecv = Socket->Recv(RecvBuffer.GetData(), RecvBuffer.Num(), ReadBytes);
			if (bRecv && ReadBytes > 0)
			{
				UE_LOG(LogTemp, Log, TEXT("[FNetworkWorker] Received %d bytes from Server."), ReadBytes);
				// 수신 바이트 로깅은 너무 많을 수 있으니 화면 출력은 생략하거나 작게 유지
				
				// Protobuf Parsing Loop
				int32 ProcessedBytes = 0;
				while (ProcessedBytes < ReadBytes)
				{
					if (ReadBytes - ProcessedBytes < sizeof(PacketHeader))
						break; // 헤더조차 다 안 왔음

					PacketHeader* Header = reinterpret_cast<PacketHeader*>(&RecvBuffer[ProcessedBytes]);
					if (ReadBytes - ProcessedBytes < Header->size)
						break; // 패킷 바디가 덜 왔음

					// 패킷 바이트를 TArray로 복사 (백그라운드 스레드의 버퍼는 다음 Recv에서 덮어써지므로)
					TArray<uint8> PacketCopy;
					PacketCopy.Append(&RecvBuffer[ProcessedBytes], Header->size);

					// GameInstance 약참조 캡처 (람다에서 사용)
					TWeakObjectPtr<UAMC1GameInstance> WeakGI = WeakGameInstance;

					// ★ GameThread로 디스패치 (Actor Spawn 등 UObject 안전 처리)
					AsyncTask(ENamedThreads::GameThread, [PacketCopy = MoveTemp(PacketCopy), WeakGI]()
					{
						if (UAMC1GameInstance* GI = WeakGI.Get())
						{
							ClientPacketHandler::GGameInstance = GI;

							std::span<std::byte> Span(
								reinterpret_cast<std::byte*>(const_cast<uint8*>(PacketCopy.GetData())),
								PacketCopy.Num()
							);
							PacketSessionRef DummySession = nullptr;
							ClientPacketHandler::HandlePacket(DummySession, Span);

							ClientPacketHandler::GGameInstance = nullptr;
						}
					});

					ProcessedBytes += Header->size;
				}
			}
		}
		else
		{
			// 너무 잦은 루핑을 방지하기 위한 대기 (1ms)
			FPlatformProcess::Sleep(0.001f);
		}
	}

	return 0;
}

void FNetworkWorker::Stop()
{
	bRunning = false;
	UE_LOG(LogTemp, Log, TEXT("[FNetworkWorker] Thread Stop Requested"));
}

void FNetworkWorker::Exit()
{
	UE_LOG(LogTemp, Log, TEXT("[FNetworkWorker] Thread Exited"));
}

void FNetworkWorker::Destroy()
{
	Stop();

	if (Thread != nullptr)
	{
		Thread->WaitForCompletion();
		delete Thread;
		Thread = nullptr;
	}
}
