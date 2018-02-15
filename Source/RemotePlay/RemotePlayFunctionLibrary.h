// (c) 2018 Simul.co

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RemotePlayFunctionLibrary.generated.h"

UCLASS()
class REMOTEPLAY_API URemotePlayFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category=RemotePlay, meta=(WorldContext="WorldContextObject"))
	static void SetStreamingLevelVisibility(UObject* WorldContextObject, const FName LevelName, bool bVisible);
};
