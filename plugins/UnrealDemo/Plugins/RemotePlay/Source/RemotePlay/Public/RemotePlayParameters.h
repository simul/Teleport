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
	int32 DepthWidth;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category=RemotePlay)
	int32 DepthHeight;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category=RemotePlay)
	uint32 bWriteDepthTexture:1; 
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category=RemotePlay)
	uint32 bStackDepth :1;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category=RemotePlay)
	uint32 bDecomposeCube :1;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category=RemotePlay)
	float MaxDepth;
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

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = RemotePlay)
	int32 ClientBandwidthLimit;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = RemotePlay)
	int32 ClientBufferSize;
};
