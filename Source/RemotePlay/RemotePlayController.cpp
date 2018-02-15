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
		}
		else
		{
			ARemotePlayWorldSettings* WorldSettings = CastChecked<ARemotePlayWorldSettings>(GetWorld()->GetWorldSettings());
			URemotePlayFunctionLibrary::SetStreamingLevelVisibility(this, WorldSettings->ServerSideLevelName, false);
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
