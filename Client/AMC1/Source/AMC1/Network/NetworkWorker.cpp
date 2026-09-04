#include "NetworkWorker.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "HAL/RunnableThread.h"
#include "ClientPacketHandler.h"
#include "ClientPacketSession.h"
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
							/*
							 * [2026-09-04] 전역 대입 대신 세션에 소유자를 실어 넘깁니다. - 결함 UE-1
							 *
							 * 변경 전에는 여기서 ClientPacketHandler::GGameInstance에 GI를 넣고
							 * 처리 직후 nullptr로 지웠습니다. 즉 static을 "람다에서 핸들러로 값을
							 * 넘기는 통로"로 쓰고 있었고, 정작 진짜 넘길 자리(PacketSessionRef)에는
							 * nullptr을 넣고 있었습니다.
							 *
							 * 단일 프로세스 PIE에서 창을 여럿 띄우면 각 창의 GameInstance가 그
							 * 하나뿐인 static을 서로 덮어써, 모든 연결의 패킷이 마지막
							 * GameInstance의 월드로 흘러갔습니다. 이제 각 연결이 자기 소유자만
							 * 참조하므로 창마다 독립적으로 동작합니다.
							 *
							 * 주의: AsyncTask(GameThread) 디스패치와 PacketCopy/MoveTemp 구조는
							 *   그대로입니다. 이 변경은 "무엇을 넘기는가"만 바꾸고 "어느 스레드에서
							 *   실행하는가"는 건드리지 않습니다. 디스패치를 없애면 SpawnActor에서
							 *   check(IsInGameThread()) assertion으로 즉시 크래시합니다.
							 */
							std::span<std::byte> Span(
								reinterpret_cast<std::byte*>(const_cast<uint8*>(PacketCopy.GetData())),
								PacketCopy.Num()
							);
							PacketSessionRef Session = MakeShared<FClientPacketSession>(GI);
							ClientPacketHandler::HandlePacket(Session, Span);
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
