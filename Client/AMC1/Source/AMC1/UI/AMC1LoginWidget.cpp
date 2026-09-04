#include "AMC1LoginWidget.h"

#include "Components/EditableTextBox.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "../AMC1GameInstance.h"

void UAMC1LoginWidget::NativeConstruct()
{
	Super::NativeConstruct();

	UAMC1GameInstance* GI = GetGameInstance<UAMC1GameInstance>();

	if (LoginButton)
	{
		// AddUniqueDynamic: NativeConstruct가 두 번 불려도 핸들러가 중복 등록되지 않습니다.
		LoginButton->OnClicked.AddUniqueDynamic(this, &UAMC1LoginWidget::OnLoginClicked);
	}

	if (GI)
	{
		GI->OnLoginResult.AddUniqueDynamic(this, &UAMC1LoginWidget::OnLoginResultReceived);

		/*
		 * 기본값 미리 채우기.
		 * GI->AccountName은 이미 커맨드라인 > .ini > UPROPERTY 순으로 해석된 값입니다
		 * (GameInstance::Init에서 ResolveServerAddress를 먼저 호출합니다).
		 * 덕분에 -AccountName=test02 로 띄우면 그 값이 입력란에 그대로 보입니다.
		 */
		if (bPrefillFromGameInstance)
		{
			if (AccountBox)  { AccountBox->SetText(FText::FromString(GI->AccountName)); }
			if (PasswordBox) { PasswordBox->SetText(FText::FromString(GI->Password)); }
		}
	}

	SetStatus(TEXT("계정을 입력하고 로그인하십시오."), /*bIsError=*/false);
}

void UAMC1LoginWidget::NativeDestruct()
{
	/*
	 * 델리게이트 해제.
	 * GameInstance는 이 위젯보다 오래 살아남으므로, 해제하지 않으면 파괴된 위젯을
	 * 향해 브로드캐스트가 날아갑니다. 동적 델리게이트는 UObject 유효성을 확인하지만
	 * 등록 목록이 계속 자라는 것을 막기 위해서라도 명시적으로 떼는 편이 좋습니다.
	 */
	if (UAMC1GameInstance* GI = GetGameInstance<UAMC1GameInstance>())
	{
		GI->OnLoginResult.RemoveDynamic(this, &UAMC1LoginWidget::OnLoginResultReceived);
	}

	if (LoginButton)
	{
		LoginButton->OnClicked.RemoveDynamic(this, &UAMC1LoginWidget::OnLoginClicked);
	}

	Super::NativeDestruct();
}

void UAMC1LoginWidget::OnLoginClicked()
{
	UAMC1GameInstance* GI = GetGameInstance<UAMC1GameInstance>();
	if (GI == nullptr)
	{
		SetStatus(TEXT("GameInstance를 찾을 수 없습니다. 프로젝트 설정을 확인하십시오."), true);
		return;
	}

	const FString Account  = AccountBox  ? AccountBox->GetText().ToString()  : FString();
	const FString Password = PasswordBox ? PasswordBox->GetText().ToString() : FString();

	/*
	 * 중복 요청 방지.
	 * HTTP 응답이 오기 전에 다시 누르면 요청이 두 번 나가고 티켓이 두 장 발급됩니다.
	 * 티켓은 1회용이라 나중 것만 유효해지고, 먼저 보낸 경로는 조용히 실패합니다.
	 * 원인을 찾기 어려운 종류라 입구에서 막습니다.
	 */
	SetBusy(true);
	SetStatus(FString::Printf(TEXT("'%s' 로 로그인 중..."), *Account), false);

	GI->SubmitLogin(Account, Password);
}

void UAMC1LoginWidget::OnLoginResultReceived(bool bSuccess, const FString& Message)
{
	if (bSuccess)
	{
		// 성공 시에는 잠금을 풀지 않습니다. 곧 레벨이 바뀌면서 이 위젯은 사라집니다.
		// 여기서 풀어주면 전환 직전에 한 번 더 눌릴 수 있습니다.
		SetStatus(FString::Printf(TEXT("로그인 성공 (%s). 접속 중..."), *Message), false);
		return;
	}

	SetBusy(false);
	SetStatus(FString::Printf(TEXT("로그인 실패: %s"), *Message), true);
}

void UAMC1LoginWidget::SetStatus(const FString& Message, bool bIsError)
{
	if (StatusText == nullptr)
	{
		return;
	}

	StatusText->SetText(FText::FromString(Message));
	StatusText->SetColorAndOpacity(
		FSlateColor(bIsError ? FLinearColor(0.85f, 0.25f, 0.20f) : FLinearColor(0.75f, 0.80f, 0.85f)));
}

void UAMC1LoginWidget::SetBusy(bool bBusy)
{
	if (LoginButton)
	{
		LoginButton->SetIsEnabled(!bBusy);
	}
	if (AccountBox)
	{
		AccountBox->SetIsEnabled(!bBusy);
	}
	if (PasswordBox)
	{
		PasswordBox->SetIsEnabled(!bBusy);
	}
}
