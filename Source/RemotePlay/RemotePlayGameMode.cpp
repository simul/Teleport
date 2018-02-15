// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#include "RemotePlayGameMode.h"
#include "RemotePlayHUD.h"
#include "RemotePlayCharacter.h"
#include "RemotePlayWorldSettings.h"
#include "RemotePlay.h"

#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/LevelStreaming.h"

ARemotePlayGameMode::ARemotePlayGameMode()
	: Super()
{
}
	
void ARemotePlayGameMode::BeginPlay()
{
	Super::BeginPlay();

	ARemotePlayWorldSettings* WorldSettings = CastChecked<ARemotePlayWorldSettings>(GetWorld()->GetWorldSettings());
	UGameplayStatics::LoadStreamLevel(this, WorldSettings->ServerSideLevelName, true, true, FLatentActionInfo{});
}
	
void ARemotePlayGameMode::NotifyClientPawnChanged(APawn* ClientPawn)
{
	OnClientPawnChanged.Broadcast(ClientPawn);
}
