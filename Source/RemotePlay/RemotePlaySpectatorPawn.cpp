// (c) 2018 Simul.co

#include "RemotePlaySpectatorPawn.h"
#include "RemotePlayGameMode.h"
#include "RemotePlay.h"

#include "Engine/World.h"
#include "Camera/CameraComponent.h"
#include "Components/SphereComponent.h"

ARemotePlaySpectatorPawn::ARemotePlaySpectatorPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bBlockInput = true;

	GetCollisionComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("SpectatorCamera"));
	RootComponent = CameraComponent;
}
	
void ARemotePlaySpectatorPawn::BeginPlay()
{
	Super::BeginPlay();

	ARemotePlayGameMode* GameMode = GetWorld()->GetAuthGameMode<ARemotePlayGameMode>();
	if(GameMode)
	{
		GameMode->OnClientPawnChanged.AddUObject(this, &ARemotePlaySpectatorPawn::SetClientPawn);
	}
}

void ARemotePlaySpectatorPawn::Tick(float DeltaTime)
{
	if(ClientPawn && ClientPawn->HasActiveCameraComponent())
	{
		const UCameraComponent* CameraComponent = CastChecked<UCameraComponent>(ClientPawn->GetComponentByClass(UCameraComponent::StaticClass()));

		FTransform ViewTransform;
		ViewTransform.SetLocation(CameraComponent->GetComponentLocation());
		ViewTransform.SetRotation(ClientPawn->GetViewRotation().Quaternion());
		SetActorTransform(ViewTransform);
	}
}
	
void ARemotePlaySpectatorPawn::SetClientPawn(APawn* InClientPawn)
{
	ClientPawn = InClientPawn;
	SetActorTickEnabled(ClientPawn != nullptr);
}
