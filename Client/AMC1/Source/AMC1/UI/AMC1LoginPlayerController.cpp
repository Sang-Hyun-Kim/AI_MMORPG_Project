#include "AMC1LoginPlayerController.h"

#include "Blueprint/UserWidget.h"
#include "../AMC1GameInstance.h"

AAMC1LoginPlayerController::AAMC1LoginPlayerController()
{
	// 로그인 화면에서는 마우스로 입력란을 클릭해야 하므로 커서를 켜 둡니다.
	bShowMouseCursor = true;
	// 조작할 폰이 없으므로 매 틱 돌 이유가 없습니다.
	PrimaryActorTick.bCanEverTick = false;
}

void AAMC1LoginPlayerController::BeginPlay()
{
	Super::BeginPlay();

	/*
	 * 입력을 UI 로만 보냅니다.
	 *
	 * 이것이 없으면 키 입력이 게임 쪽으로도 흘러갑니다. 초판에서
	 * "로그인 화면인데 입력이 전부 캐릭터 이동으로 먹히는" 증상의 원인이었습니다.
	 * (전용 GameMode 로 폰 스폰을 막았더라도, 입력 모드를 명시하지 않으면
	 *  포커스가 위젯으로 가지 않아 타이핑이 안 됩니다.)
	 */
	FInputModeUIOnly InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);
	bShowMouseCursor = true;

	CreateAndShowLoginWidget();
}

void AAMC1LoginPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 레벨 전환 시 위젯도 함께 사라지지만 명시적으로 내립니다.
	// (남겨두면 다음 레벨의 화면 위에 잔상이 남을 수 있습니다.)
	if (LoginWidget)
	{
		LoginWidget->RemoveFromParent();
		LoginWidget = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}

void AAMC1LoginPlayerController::CreateAndShowLoginWidget()
{
	UAMC1GameInstance* GI = GetGameInstance<UAMC1GameInstance>();
	if (GI == nullptr)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[LoginPC] GameInstance is not UAMC1GameInstance. "
			     "프로젝트 설정의 GameInstanceClass 를 확인하십시오."));
		return;
	}

	if (GI->LoginWidgetClass.IsNull())
	{
		/*
		 * 위젯이 지정되지 않은 경우.
		 * GameInstance::Init 이 이미 자동 로그인을 수행했을 것이므로 여기서는
		 * 아무것도 하지 않습니다. 다만 로그인 레벨에 이 컨트롤러를 붙여 놓고
		 * 위젯을 지정하지 않은 것은 설정 실수일 가능성이 높으므로 경고를 남깁니다.
		 */
		UE_LOG(LogTemp, Warning,
			TEXT("[LoginPC] LoginWidgetClass is not set. "
			     "DefaultGame.ini 의 [/Script/AMC1.AMC1GameInstance] 섹션을 확인하십시오."));
		return;
	}

	UClass* WidgetClass = GI->LoginWidgetClass.LoadSynchronous();
	if (WidgetClass == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("[LoginPC] LoginWidgetClass failed to load: %s"),
			*GI->LoginWidgetClass.ToString());
		return;
	}

	LoginWidget = CreateWidget<UUserWidget>(this, WidgetClass);
	if (LoginWidget == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("[LoginPC] CreateWidget returned null."));
		return;
	}

	LoginWidget->AddToViewport();

	// GameInstance 쪽 경로가 위젯을 중복 생성하지 않도록 소유권을 알려 줍니다.
	GI->NotifyLoginWidgetShown(LoginWidget);

	UE_LOG(LogTemp, Log, TEXT("[LoginPC] Login widget shown (%s)."), *WidgetClass->GetName());
}
