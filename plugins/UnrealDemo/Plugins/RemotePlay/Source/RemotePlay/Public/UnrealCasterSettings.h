// Copyright 2018 Simul.co

#pragma once

#include "CoreMinimal.h"

#include "SimulCasterServer/CasterSettings.h"

#include "UnrealCasterSettings.generated.h"

USTRUCT(BlueprintType)
struct FUnrealCasterNetworkSettings
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
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = RemotePlay)
	int32 RequiredLatencyMs;

	SCServer::CasterNetworkSettings GetAsCasterNetworkSettings()
	{
		return
		{
			LocalPort,
			*RemoteIP,
			RemotePort,
			ClientBandwidthLimit,
			ClientBufferSize,
			RequiredLatencyMs
		};
	}
};


USTRUCT(BlueprintType)
struct FUnrealCasterEncoderSettings
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = RemotePlay)
	int32 FrameWidth;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = RemotePlay)
	int32 FrameHeight;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = RemotePlay)
	int32 DepthWidth;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = RemotePlay)
	int32 DepthHeight;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = RemotePlay)
	bool bWriteDepthTexture;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = RemotePlay)
	bool bStackDepth;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = RemotePlay)
	bool bDecomposeCube;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = RemotePlay)
	float MaxDepth;

	SCServer::CasterEncoderSettings GetAsCasterEncoderSettings()
	{
		return
		{
			FrameWidth,
			FrameHeight,
			DepthWidth,
			DepthHeight,
			bWriteDepthTexture,
			bStackDepth,
			bDecomposeCube,
			MaxDepth
		};
	}
};