#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "AMC1LoginPlayerController.generated.h"

class UUserWidget;

/*
 * AAMC1LoginPlayerController — 로그인 화면 전용 컨트롤러 (2026-09-04 신설)
 * ────────────────────────────────────────────────────────────────────────────
 * [왜 필요한가 — 초판 설계의 잘못]
 *
 *   초판은 위젯 생성을 UAMC1GameInstance 가 PostLoadMapWithWorld 에서 처리했습니다.
 *   그런데 로그인 레벨에도 게임용 GameMode(BP_AMC1GameMode)가 그대로 적용되어
 *   다음 문제가 겹쳤습니다.
 *
 *     1) 로그인 화면인데 **플레이어 캐릭터가 스폰**됩니다.
 *     2) 그 캐릭터의 EnhancedInput 매핑이 살아 있어 **입력이 전부 이동으로 갑니다.**
 *        UI 가 키보드를 받지 못해 계정을 입력할 수 없습니다.
 *     3) 위젯 생성 시점이 PlayerController 준비 여부에 좌우되어 불안정했습니다.
 *
 *   레벨의 성격이 다르면 GameMode 도 달라야 합니다. 로그인 화면은
 *   **조작할 캐릭터가 없는 화면**이므로 전용 GameMode/Controller 를 둡니다.
 *
 * [이 클래스가 보장하는 것]
 *   · BeginPlay 시점에는 PlayerController 가 반드시 존재합니다 — 자기 자신이므로
 *     "PC 가 아직 없어서 위젯을 못 띄우는" 경우가 원천적으로 사라집니다.
 *   · 입력 모드를 UI 전용으로 고정하고 마우스 커서를 켭니다.
 *   · 위젯 클래스는 GameInstance 의 LoginWidgetClass 를 그대로 씁니다.
 *     설정 위치를 한 곳으로 유지하기 위함입니다.
 *
 * ⚠️ 이 컨트롤러는 **로그인 레벨에서만** 쓰입니다. 게임 레벨에는
 *    기존 AAMC1PlayerController(이동 패킷 송신 담당)가 그대로 붙습니다.
 */
UCLASS()
class AMC1_API AAMC1LoginPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AAMC1LoginPlayerController();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** GameInstance 의 LoginWidgetClass 를 읽어 위젯을 만들고 화면에 올립니다. */
	void CreateAndShowLoginWidget();

	UPROPERTY()
	TObjectPtr<UUserWidget> LoginWidget;
};
