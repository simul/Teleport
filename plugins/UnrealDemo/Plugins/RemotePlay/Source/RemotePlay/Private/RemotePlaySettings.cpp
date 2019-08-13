// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RemotePlaySettings.h"

URemotePlaySettings::URemotePlaySettings(const FObjectInitializer& ObjectInitlaizer)
	: Super(ObjectInitlaizer)
	,ClientIP("")
	,VideoEncodeFrequency(1)
	,StreamGeometry(true)
{

}

FName URemotePlaySettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

#if WITH_EDITOR
FText URemotePlaySettings::GetSectionText() const
{
	return NSLOCTEXT("RemotePlayPlugin", "RemotePlaySettingsSection", "RemotePlay");
}
#endif

#if WITH_EDITOR
void URemotePlaySettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.Property != nullptr)
	{
		SettingsChangedDelegate.Broadcast(PropertyChangedEvent.Property->GetName(), this);
	}
}

URemotePlaySettings::FOnRemotePlaySettingsChanged& URemotePlaySettings::OnSettingsChanged()
{
	return SettingsChangedDelegate;
}

URemotePlaySettings::FOnRemotePlaySettingsChanged URemotePlaySettings::SettingsChangedDelegate;
#endif


