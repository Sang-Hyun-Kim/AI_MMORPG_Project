#include "AMC1Character.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Controller.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "UObject/ConstructorHelpers.h"
#include "AMC1PlayerController.h"

AAMC1Character::AAMC1Character()
{
	PrimaryActorTick.bCanEverTick = true;

	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);

	// Don't rotate when the controller rotates. Let that just affect the camera.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = true; // Character moves in the direction of input...	
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f); // ...at this rotation rate

	GetCharacterMovement()->JumpZVelocity = 700.f;
	GetCharacterMovement()->AirControl = 0.35f;
	GetCharacterMovement()->MaxWalkSpeed = 500.f;
	GetCharacterMovement()->MinAnalogWalkSpeed = 20.f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.f;

	// Create a camera boom (pulls in towards the player if there is a collision)
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 400.0f; // The camera follows at this distance behind the character	
	CameraBoom->bUsePawnControlRotation = true; // Rotate the arm based on the controller

	// Create a follow camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName); // Attach the camera to the end of the boom
	FollowCamera->bUsePawnControlRotation = false; // Camera does not rotate relative to arm

	// Way 1: C++ Constructor 기본 에셋 주입 (ConstructorHelpers)
	static ConstructorHelpers::FObjectFinder<UInputMappingContext> IMC_Finder(TEXT("/Game/AMC1/Input/IMC_Default.IMC_Default"));
	if (IMC_Finder.Succeeded())
	{
		DefaultMappingContext = IMC_Finder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UInputAction> IA_MoveFinder(TEXT("/Game/AMC1/Input/IA_Move.IA_Move"));
	if (IA_MoveFinder.Succeeded())
	{
		MoveAction = IA_MoveFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UInputAction> IA_LookFinder(TEXT("/Game/AMC1/Input/IA_Look.IA_Look"));
	if (IA_LookFinder.Succeeded())
	{
		LookAction = IA_LookFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UInputAction> IA_AttackFinder(TEXT("/Game/AMC1/Input/IA_Attack.IA_Attack"));
	if (IA_AttackFinder.Succeeded())
	{
		AttackAction = IA_AttackFinder.Object;
	}
}

void AAMC1Character::BeginPlay()
{
	Super::BeginPlay();
}

void AAMC1Character::PawnClientRestart()
{
	Super::PawnClientRestart();

	// 컨트롤러 빙의 완료 시점 (100% 보장되는 언리얼 엔진 표준 수명주기)
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			if (DefaultMappingContext)
			{
				Subsystem->AddMappingContext(DefaultMappingContext, 0);
				UE_LOG(LogTemp, Log, TEXT("[AAMC1Character] DefaultMappingContext registered successfully in PawnClientRestart!"));
			}
		}
	}
}

void AAMC1Character::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void AAMC1Character::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// 2중 안전장치: SetupPlayerInputComponent 호출 시에도 서브시스템에 컨텍스트 확인 및 등록
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			if (DefaultMappingContext && !Subsystem->HasMappingContext(DefaultMappingContext))
			{
				Subsystem->AddMappingContext(DefaultMappingContext, 0);
				UE_LOG(LogTemp, Log, TEXT("[AAMC1Character] DefaultMappingContext registered in SetupPlayerInputComponent!"));
			}
		}
	}

	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// Moving
		if (MoveAction)
		{
			EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AAMC1Character::Move);
		}

		// Looking
		if (LookAction)
		{
			EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AAMC1Character::Look);
		}

		// Attacking
		if (AttackAction)
		{
			EnhancedInputComponent->BindAction(AttackAction, ETriggerEvent::Started, this, &AAMC1Character::Attack);
		}
	}
}

void AAMC1Character::Move(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D MovementVector = Value.Get<FVector2D>();

	if (Controller != nullptr)
	{
		// find out which way is forward
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// get forward vector
		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	
		// get right vector 
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		// add movement 
		AddMovementInput(ForwardDirection, MovementVector.Y);
		AddMovementInput(RightDirection, MovementVector.X);

		// Send move packet to game server
		if (AAMC1PlayerController* PC = Cast<AAMC1PlayerController>(Controller))
		{
			PC->SendMovePacket();
		}
	}
}

void AAMC1Character::Look(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	if (Controller != nullptr)
	{
		// add yaw and pitch input to controller
		AddControllerYawInput(LookAxisVector.X);
		AddControllerPitchInput(LookAxisVector.Y);
	}
}

void AAMC1Character::Attack()
{
	if (AAMC1PlayerController* PC = Cast<AAMC1PlayerController>(Controller))
	{
		PC->SendAttackPacket(0);
	}
}

