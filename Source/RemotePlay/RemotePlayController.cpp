// (c) 2018 Simul.co

#include "RemotePlayController.h"
#include "RemotePlayGameMode.h"
#include "RemotePlayFunctionLibrary.h"
#include "RemotePlayWorldSettings.h"
#include "RemotePlay.h"

#include "Engine/World.h"

void ARemotePlayController::BeginPlay()
{
	if(IsLocalPlayerController())
	{
		if(GetWorld()->IsServer())
		{
			if(APawn* Pawn = GetPawn())
			{
				UnPossess();
				Pawn->Destroy();
			}

			StartSpectatingOnly();
			SetServerInputMode();
		}
		else
		{
			GetWorld()->FlushLevelStreaming();

			ARemotePlayWorldSettings* WorldSettings = CastChecked<ARemotePlayWorldSettings>(GetWorld()->GetWorldSettings());
			URemotePlayFunctionLibrary::SetStreamingLevelVisibility(this, WorldSettings->ServerSideLevelName, false);

			SetClientInputMode();
		}
	}
}

void ARemotePlayController::Possess(APawn* Pawn)
{
	Super::Possess(Pawn);

	if(!IsLocalController())
	{
		ARemotePlayGameMode* GameMode = GetWorld()->GetAuthGameMode<ARemotePlayGameMode>();
		check(GameMode);
		GameMode->NotifyClientPawnChanged(Pawn);
	}
}

void ARemotePlayController::UnPossess()
{
	if(!IsLocalController())
	{
		ARemotePlayGameMode* GameMode = GetWorld()->GetAuthGameMode<ARemotePlayGameMode>();
		check(GameMode);
		GameMode->NotifyClientPawnChanged(nullptr);
	}

	Super::UnPossess();
}
	
void ARemotePlayController::SetServerInputMode()
{
	FInputModeUIOnly InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);
	bShowMouseCursor = true;
}
	
void ARemotePlayController::SetClientInputMode()
{
	FInputModeGameOnly InputMode;
	SetInputMode(InputMode);
	bShowMouseCursor = false;
}
