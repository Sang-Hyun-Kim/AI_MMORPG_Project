#include "AMC1LoginGameMode.h"
#include "AMC1LoginPlayerController.h"

AAMC1LoginGameMode::AAMC1LoginGameMode()
{
	/*
	 * 조작할 캐릭터를 만들지 않습니다.
	 *
	 * nullptr 로 두면 엔진이 폰을 스폰하지 않고, PlayerStart 가 레벨에 남아 있어도
	 * 무시됩니다. 덕분에 로그인 화면에 캐릭터가 서 있지 않고,
	 * 캐릭터가 없으니 EnhancedInput 매핑도 붙지 않아 키 입력이 UI 로만 갑니다.
	 */
	DefaultPawnClass = nullptr;

	// 위젯 생성과 UI 입력 모드를 담당하는 전용 컨트롤러.
	PlayerControllerClass = AAMC1LoginPlayerController::StaticClass();

	// 로그인 화면에는 갱신할 게임 상태가 없습니다.
	PrimaryActorTick.bCanEverTick = false;
}
