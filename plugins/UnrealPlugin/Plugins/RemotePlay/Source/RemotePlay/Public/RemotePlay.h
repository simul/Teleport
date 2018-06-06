// Copyright 2018 Simul.co

#pragma once
#include "ModuleManager.h"

class APlayerController;

class IRemotePlay : public IModuleInterface
{
public:
	static inline IRemotePlay& Get()
	{
		return FModuleManager::LoadModuleChecked<IRemotePlay>("RemotePlay");
	}

	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("RemotePlay");
	}
};
