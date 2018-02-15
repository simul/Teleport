// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#include "RemotePlayGameMode.h"
#include "RemotePlayHUD.h"
#include "RemotePlayCharacter.h"
#include "UObject/ConstructorHelpers.h"

ARemotePlayGameMode::ARemotePlayGameMode()
	: Super()
{
}
	
void ARemotePlayGameMode::NotifyClientPawnChanged(APawn* ClientPawn)
{
	OnClientPawnChanged.Broadcast(ClientPawn);
}
