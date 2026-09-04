#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "AMC1LoginGameMode.generated.h"

/*
 * AAMC1LoginGameMode — 로그인 레벨 전용 GameMode (2026-09-04 신설)
 * ────────────────────────────────────────────────────────────────────────────
 * [무엇을 바꾸는가]
 *
 *   DefaultPawnClass       = 없음            조작할 캐릭터를 스폰하지 않습니다.
 *   PlayerControllerClass  = LoginPC        위젯 생성과 UI 입력 모드를 담당합니다.
 *   HUDClass               = 기본
 *
 * [왜 필요한가]
 *
 *   프로젝트 전역 기본값(GlobalDefaultGameMode = BP_AMC1GameMode)은 게임 플레이용이라
 *   로그인 레벨에도 그대로 적용되면 다음이 벌어집니다.
 *
 *     · 로그인 화면에 **플레이어 캐릭터가 스폰**되어 UI 뒤에 서 있습니다.
 *     · 그 캐릭터의 EnhancedInput 매핑이 살아 있어 **키 입력이 전부 이동으로** 갑니다.
 *       계정을 타이핑할 수 없습니다. (2026-09-04 사용자 보고)
 *
 *   레벨의 성격이 다르면 GameMode 도 달라야 합니다.
 *
 * [적용 방법 — 코드만으로는 켜지지 않습니다]
 *   레벨 에디터에서 **World Settings → GameMode Override** 를 이 클래스로 지정해야
 *   합니다. 지정하지 않으면 전역 기본값이 그대로 쓰여 위 증상이 재발합니다.
 *   (LoginLevel 에만 지정하십시오. 로비/게임 레벨은 전역 기본값을 그대로 씁니다.)
 */
UCLASS()
class AMC1_API AAMC1LoginGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AAMC1LoginGameMode();
};
