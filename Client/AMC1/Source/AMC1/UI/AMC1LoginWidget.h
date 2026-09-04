#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "AMC1LoginWidget.generated.h"

class UEditableTextBox;
class UButton;
class UTextBlock;

/*
 * UAMC1LoginWidget — 로그인 화면의 C++ 베이스 (2026-09-04 신설 / T3)
 * ────────────────────────────────────────────────────────────────────────────
 * [사용 방법]
 *   1) 콘텐츠 브라우저에서 **이 클래스를 부모로 하는 위젯 블루프린트**를 만듭니다.
 *      (Widget Blueprint 생성 → 부모 클래스로 AMC1LoginWidget 선택)
 *   2) 그 WBP 안에 아래 **네 개의 위젯을 같은 이름으로** 배치합니다.
 *        AccountBox    : Editable Text Box
 *        PasswordBox   : Editable Text Box   (Is Password 체크 권장)
 *        LoginButton   : Button
 *        StatusText    : Text Block
 *   3) 프로젝트 설정 또는 DefaultGame.ini의
 *        [/Script/AMC1.AMC1GameInstance]
 *        LoginWidgetClass=/Game/.../WBP_Login.WBP_Login_C
 *      에 그 WBP를 지정합니다.
 *
 * [이름이 틀리면 어떻게 되는가]
 *   meta=(BindWidget)은 **필수 바인딩**입니다. WBP에 같은 이름·같은 타입의
 *   위젯이 없으면 **위젯 블루프린트 컴파일 단계에서 에러**가 납니다.
 *   즉 런타임에 조용히 null이 되는 것이 아니라 에디터가 먼저 알려줍니다.
 *   (선택 바인딩이 필요하면 meta=(BindWidgetOptional)을 쓰십시오.)
 *
 * [왜 C++ 베이스를 두는가]
 *   로그인 흐름(계정 검증 → 티켓 수령 → 접속 → 레벨 전환)은 전부 C++에 있습니다.
 *   위젯이 그 흐름을 블루프린트로 재구현하면 두 벌이 되어 어긋납니다.
 *   여기서는 **입력을 모아 GameInstance에 넘기고 결과를 표시**하는 일만 합니다.
 *
 * [연결 구조]
 *   LoginButton 클릭 → UAMC1GameInstance::SubmitLogin(계정, 비번)
 *   UAMC1GameInstance::OnLoginResult → OnLoginResultReceived → StatusText 갱신
 */
UCLASS()
class AMC1_API UAMC1LoginWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/** 계정명 입력란. WBP에 같은 이름으로 존재해야 합니다. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UEditableTextBox> AccountBox;

	/** 비밀번호 입력란. WBP에서 Is Password를 켜두십시오. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UEditableTextBox> PasswordBox;

	/** 로그인 버튼. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> LoginButton;

	/** 진행 상황·실패 사유 표시. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> StatusText;

	/*
	 * 처음 열릴 때 입력란에 채워 넣을 기본 계정.
	 * GameInstance의 AccountName(커맨드라인 > .ini > 기본값으로 이미 해석된 값)을
	 * 가져다 쓰므로, 테스트할 때 매번 타자를 치지 않아도 됩니다.
	 * 비워두고 싶으면 false로 두십시오.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Login")
	bool bPrefillFromGameInstance = true;

private:
	UFUNCTION()
	void OnLoginClicked();

	/** GameInstance의 OnLoginResult에 바인딩됩니다. */
	UFUNCTION()
	void OnLoginResultReceived(bool bSuccess, const FString& Message);

	void SetStatus(const FString& Message, bool bIsError);

	/** 요청 중 버튼을 잠가 중복 요청을 막습니다. */
	void SetBusy(bool bBusy);
};
