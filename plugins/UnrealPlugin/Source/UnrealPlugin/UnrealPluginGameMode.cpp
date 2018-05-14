// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "UnrealPluginGameMode.h"
#include "UnrealPluginHUD.h"
#include "UnrealPluginCharacter.h"
#include "UObject/ConstructorHelpers.h"

AUnrealPluginGameMode::AUnrealPluginGameMode()
	: Super()
{
	// set default pawn class to our Blueprinted character
	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnClassFinder(TEXT("/Game/FirstPersonCPP/Blueprints/FirstPersonCharacter"));
	DefaultPawnClass = PlayerPawnClassFinder.Class;

	// use our custom HUD class
	HUDClass = AUnrealPluginHUD::StaticClass();
}
