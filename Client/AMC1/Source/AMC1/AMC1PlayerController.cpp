#include "AMC1PlayerController.h"
#include "Network/ClientPacketHandler.h"
#include "AMC1GameInstance.h"
#include "Manager/AMC1ObjectManager.h"

void AAMC1PlayerController::BeginPlay()
{
	Super::BeginPlay();

	/*
	 * [2026-09-04 / 결함 UE-6] 게임 입력 모드를 **명시적으로 되찾습니다.**
	 *
	 * ── 증상 ──────────────────────────────────────────────────────────────
	 *   로그인 → 로비 레벨 전환 후 **캐릭터가 전혀 조작되지 않음.**
	 *   그런데 실행 중인 PIE 월드를 측정해 보면 모든 것이 정상이었습니다.
	 *
	 *     WORLD                 /Game/Level/UEDPIE_0_LobbyLevel
	 *     GAMEMODE              BP_AMC1GameMode_C          (게임용, 정상)
	 *     PAWN                  BP_AMC1Character_C_0       (존재하고 Possess됨)
	 *     PAWN LOC              (656.7, 96.3, 98.2)        (DB 복원 좌표, 정상)
	 *     is_move_input_ignored False
	 *     MovementMode          MOVE_WALKING
	 *     MappingContext        등록 완료 (PawnClientRestart 로그 확인)
	 *
	 *   즉 게임 상태에는 잘못된 곳이 하나도 없었습니다.
	 *
	 * ── 원인 ──────────────────────────────────────────────────────────────
	 *   `SetInputMode()`가 바꾸는 것은 PlayerController의 멤버가 아니라
	 *   **뷰포트(FSlateApplication / GameViewportClient)의 입력 캡처 상태**입니다.
	 *   로그인 레벨의 AAMC1LoginPlayerController가 FInputModeUIOnly 로 바꿔 두면,
	 *   레벨이 전환되어 **새 PlayerController가 만들어져도 그 뷰포트 상태는
	 *   그대로 남습니다.** 새 컨트롤러가 게임 입력을 요구하지 않기 때문입니다.
	 *
	 *   bShowMouseCursor 는 새 컨트롤러의 기본값(false)이라 정상으로 보이는데,
	 *   정작 뷰포트는 키 입력을 게임으로 보내지 않는 상태였습니다.
	 *   **읽히는 값은 전부 정상인데 조작만 안 되는** 추적하기 까다로운 유형입니다.
	 *
	 * ── 유지 지침 ─────────────────────────────────────────────────────────
	 *   ⚠️ 이 블록을 지우지 마십시오. UI를 띄우는 화면(로그인, 캐릭터 선택, 상점 등)이
	 *      늘어날수록 "UI 모드로 바꾼 뒤 게임으로 돌아오지 못하는" 경로가 함께 늘어납니다.
	 *      **게임플레이 컨트롤러가 시작할 때 자기 입력 모드를 스스로 주장하는 것**이
	 *      그 모든 경우를 한 곳에서 막아 줍니다.
	 */
	FInputModeGameOnly InputMode;
	InputMode.SetConsumeCaptureMouseDown(true);
	SetInputMode(InputMode);
	bShowMouseCursor = false;

	UE_LOG(LogTemp, Log, TEXT("[AMC1PlayerController] Game input mode asserted."));
}

void AAMC1PlayerController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void AAMC1PlayerController::SendMovePacket()
{
	APawn* MyPawn = GetPawn();
	if (!MyPawn) return;

	FVector Location = MyPawn->GetActorLocation();
	float Yaw = MyPawn->GetControlRotation().Yaw;

	UAMC1GameInstance* GameInstance = Cast<UAMC1GameInstance>(GetGameInstance());
	if (GameInstance)
	{
		Protocol::C_MOVE pkt;
		Protocol::PositionInfo* posInfo = pkt.mutable_posinfo();
		posInfo->set_x(Location.X);
		posInfo->set_y(Location.Y);
		posInfo->set_z(Location.Z);
		posInfo->set_yaw(Yaw);

		SendBufferRef sendBuffer = ClientPacketHandler::MakeSendBuffer(pkt);
		GameInstance->SendPacket(sendBuffer);

		uint64 MyId = 0;
		if (UAMC1ObjectManager* ObjMgr = GameInstance->GetSubsystem<UAMC1ObjectManager>())
		{
			MyId = ObjMgr->GetMyPlayerId();
		}

		UE_LOG(LogTemp, Log, TEXT("[PlayerController] C_MOVE Sent [Player: %llu]: (X: %f, Y: %f, Z: %f, Yaw: %f)"), MyId, Location.X, Location.Y, Location.Z, Yaw);

		// Key=100 고정 라인에서 내 이동 좌표 실시간 갱신 (화면 도배 원천 방지)
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(100, 2.0f, FColor::Yellow, 
				FString::Printf(TEXT("[Send] C_MOVE [Player: %llu] Pos: (%.1f, %.1f, %.1f)"), MyId, Location.X, Location.Y, Location.Z));
		}
	}
}

void AAMC1PlayerController::SendAttackPacket(int32 TargetId)
{
	UAMC1GameInstance* GameInstance = Cast<UAMC1GameInstance>(GetGameInstance());
	if (GameInstance)
	{
		Protocol::C_ATTACK pkt;
		pkt.set_targetid(TargetId);

		SendBufferRef sendBuffer = ClientPacketHandler::MakeSendBuffer(pkt);
		GameInstance->SendPacket(sendBuffer);

		UE_LOG(LogTemp, Log, TEXT("[PlayerController] C_ATTACK Sent: (TargetId: %d)"), TargetId);
		if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Yellow, FString::Printf(TEXT("[Send] C_ATTACK: (TargetId: %d)"), TargetId));

	}
}
