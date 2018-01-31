#include "RemotePlayGameMode.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/PlayerState.h"
#include "LogMacros.h"

DEFINE_LOG_CATEGORY(LogRemotePlay);

ARemotePlayGameMode::ARemotePlayGameMode(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ServerPlayerControllerClass = APlayerController::StaticClass();
	ServerPlayerStateClass = APlayerState::StaticClass();
}

APlayerController* ARemotePlayGameMode::SpawnPlayerController(ENetRole InRemoteRole, FVector const& SpawnLocation, FRotator const& SpawnRotation)
{
	if(GetWorld()->GetNetMode()==NM_ListenServer&&InRemoteRole==ENetRole::ROLE_SimulatedProxy)
	// If we're a server, we need to spawn the Server Player Controller.
	{
		FActorSpawnParameters SpawnInfo;
		SpawnInfo.Instigator = Instigator;
		SpawnInfo.ObjectFlags |= RF_Transient;	// We never want to save player controllers into a map
		SpawnInfo.bDeferConstruction = true;
		APlayerController* NewPC = GetWorld()->SpawnActor<APlayerController>(ServerPlayerControllerClass, SpawnLocation, SpawnRotation, SpawnInfo);
		if (NewPC)
		{
			if (InRemoteRole == ROLE_SimulatedProxy)
			{
				// This is a local player because it has no authority/autonomous remote role
				NewPC->SetAsLocalPlayerController();
			}

			UGameplayStatics::FinishSpawningActor(NewPC, FTransform(SpawnRotation, SpawnLocation));
			NewPC->PlayerState->bOnlySpectator=true;
		}

		return NewPC;
	}
	else
	{
		return AGameModeBase::SpawnPlayerController( InRemoteRole,SpawnLocation,SpawnRotation);
	}
}

void ARemotePlayGameMode::RestartPlayer(AController* NewPlayer)
{
	if (NewPlayer == nullptr || NewPlayer->IsPendingKillPending())
	{
		return;
	}

	AActor* StartSpot = FindPlayerStart(NewPlayer);

	// If a start spot wasn't found,
	if (StartSpot == nullptr)
	{
		// Check for a previously assigned spot
		if (NewPlayer->StartSpot != nullptr)
		{
			StartSpot = NewPlayer->StartSpot.Get();
			UE_LOG(LogRemotePlay, Warning, TEXT("RestartPlayer: Player start not found, using last start spot"));
		}	
	}

	RestartPlayerAtPlayerStart(NewPlayer, StartSpot);
}

void ARemotePlayGameMode::RestartPlayerAtPlayerStart(AController* NewPlayer, AActor* StartSpot) 
{
	if (NewPlayer == nullptr || NewPlayer->IsPendingKillPending())
	{
		return;
	}

	if (!StartSpot)
	{
		UE_LOG(LogRemotePlay, Warning, TEXT("RestartPlayerAtPlayerStart: Player start not found"));
		return;
	}

	FRotator SpawnRotation = StartSpot->GetActorRotation();

	UE_LOG(LogRemotePlay, Verbose, TEXT("RestartPlayerAtPlayerStart %s"), NewPlayer->PlayerState ? *NewPlayer->PlayerState->PlayerName : TEXT("Unknown"));

	if (MustSpectate(Cast<APlayerController>(NewPlayer)))
	{
		UE_LOG(LogRemotePlay, Verbose, TEXT("RestartPlayerAtPlayerStart: Tried to restart a spectator-only player!"));
		return;
	}

	if (NewPlayer->GetPawn() != nullptr)
	{
		// If we have an existing pawn, just use it's rotation
		SpawnRotation = NewPlayer->GetPawn()->GetActorRotation();
	}
	else if (GetDefaultPawnClassForController(NewPlayer) != nullptr)
	{
		// Try to create a pawn to use of the default class for this player
		NewPlayer->SetPawn(SpawnDefaultPawnFor(NewPlayer, StartSpot));
	}

	if (NewPlayer->GetPawn() == nullptr)
	{
		NewPlayer->FailedToSpawnPawn();
	}
	else
	{
		// Tell the start spot it was used
		InitStartSpot(StartSpot, NewPlayer);

		FinishRestartPlayer(NewPlayer, SpawnRotation);
	}
}

void ARemotePlayGameMode::RestartPlayerAtTransform(AController* NewPlayer, const FTransform& SpawnTransform) 
{
	if (NewPlayer == nullptr || NewPlayer->IsPendingKillPending())
	{
		return;
	}

	UE_LOG(LogRemotePlay, Verbose, TEXT("RestartPlayerAtTransform %s"), NewPlayer->PlayerState ? *NewPlayer->PlayerState->PlayerName : TEXT("Unknown"));

	if (MustSpectate(Cast<APlayerController>(NewPlayer)))
	{
		UE_LOG(LogRemotePlay, Verbose, TEXT("RestartPlayerAtTransform: Tried to restart a spectator-only player!"));
		return;
	}

	FRotator SpawnRotation = SpawnTransform.GetRotation().Rotator();

	if (NewPlayer->GetPawn() != nullptr)
	{
		// If we have an existing pawn, just use it's rotation
		SpawnRotation = NewPlayer->GetPawn()->GetActorRotation();
	}
	else if (GetDefaultPawnClassForController(NewPlayer) != nullptr)
	{
		// Try to create a pawn to use of the default class for this player
		NewPlayer->SetPawn(SpawnDefaultPawnAtTransform(NewPlayer, SpawnTransform));
	}

	if (NewPlayer->GetPawn() == nullptr)
	{
		NewPlayer->FailedToSpawnPawn();
	}
	else
	{
		FinishRestartPlayer(NewPlayer, SpawnRotation);
	}
}