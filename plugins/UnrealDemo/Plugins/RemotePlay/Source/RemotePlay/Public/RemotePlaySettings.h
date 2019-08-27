// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DeveloperSettings.h"
#include "InputCoreTypes.h"
#include "RemotePlaySettings.generated.h"


UCLASS(config = RemotePlay, defaultconfig, meta = (DisplayName = "RemotePlay"))
class REMOTEPLAY_API URemotePlaySettings : public UDeveloperSettings
{
	GENERATED_UCLASS_BODY()

	UPROPERTY(config, EditAnywhere, Category = RemotePlay)
	FString ClientIP;

	UPROPERTY(config, EditAnywhere, Category = RemotePlay)
	int32 VideoEncodeFrequency;

	UPROPERTY(config, EditAnywhere, Category = RemotePlay)
	uint32 StreamGeometry : 1;

	UPROPERTY(config, EditAnywhere, Category = RemotePlay)
	uint32 StreamGeometryContinuously : 1;

	// Begin UDeveloperSettings Interface
	virtual FName GetCategoryName() const override;
#if WITH_EDITOR
	virtual FText GetSectionText() const override;
#endif
	// END UDeveloperSettings Interface

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

public:
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnRemotePlaySettingsChanged, const FString&, const URemotePlaySettings*);

	/** Gets a multicast delegate which is called whenever one of the parameters in this settings object changes. */
	static FOnRemotePlaySettingsChanged& OnSettingsChanged();

protected:
	static FOnRemotePlaySettingsChanged SettingsChangedDelegate;
#endif
};