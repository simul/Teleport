// (c) 2018 Simul.co

#pragma once

#include "CoreMinimal.h"
#include "RemotePlayTypes.generated.h"

USTRUCT(BlueprintType)
struct FRemotePlayStreamParameters
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="RemotePlay")
	int32 FrameWidth;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="RemotePlay")
	int32 FrameHeight;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="RemotePlay")
	int32 ConnectionPort;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="RemotePlay")
	int32 IDRFrequency;
};
