// (c) 2018 Simul.co

#include "RemotePlayController.h"
#include "RemotePlayGameMode.h"
#include "RemotePlay.h"

#include "Engine/World.h"

void ARemotePlayController::BeginPlay()
{
	if(GetWorld()->IsServer() && IsLocalPlayerController())
	{
		StartSpectatingOnly();
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
