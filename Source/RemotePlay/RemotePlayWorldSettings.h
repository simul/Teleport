// (c) 2018 Simul.co

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/WorldSettings.h"
#include "RemotePlayWorldSettings.generated.h"

UCLASS()
class REMOTEPLAY_API ARemotePlayWorldSettings : public AWorldSettings
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Config, Category="RemotePlay")
	FName ServerSideLevelName;
};
