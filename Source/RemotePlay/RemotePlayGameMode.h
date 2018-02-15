// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "RemotePlayGameMode.generated.h"

UCLASS(minimalapi)
class ARemotePlayGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	ARemotePlayGameMode();

	/* Begin AActor interface */
	virtual void BeginPlay() override;
	/* End AActor interface */

	DECLARE_MULTICAST_DELEGATE_OneParam(FClientPawnChangedDelegate, APawn*);
	FClientPawnChangedDelegate OnClientPawnChanged;

	void NotifyClientPawnChanged(APawn* ClientPawn);
};
