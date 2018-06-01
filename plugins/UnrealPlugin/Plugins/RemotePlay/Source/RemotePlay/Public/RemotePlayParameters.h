// Copyright 2018 Simul.co

#pragma once

#include "CoreMinimal.h"
#include "RemotePlayParameters.generated.h"

USTRUCT(BlueprintType)
struct FRemotePlayEncodeParameters
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category=RemotePlay)
	int32 FrameWidth;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category=RemotePlay)
	int32 FrameHeight;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category=RemotePlay)
	int32 IDRInterval;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category=RemotePlay)
	int32 TargetFPS;
};

USTRUCT(BlueprintType)
struct FRemotePlayNetworkParameters
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category=RemotePlay)
	int32 LocalPort;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category=RemotePlay)
	FString RemoteIP;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category=RemotePlay)
	int32 RemotePort;
};
