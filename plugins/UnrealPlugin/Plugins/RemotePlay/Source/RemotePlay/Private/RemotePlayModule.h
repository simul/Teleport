// Copyright 2018 Simul.co

#pragma once

#include "CoreMinimal.h"
#include "RemotePlay.h"

#include "libavstream/libavstream.hpp"

DECLARE_LOG_CATEGORY_EXTERN(LogRemotePlay, Log, All);

class FRemotePlayModule : public IRemotePlay
{
public:
	/* Begin IModuleInterface */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	/* End IModuleInterface */
	
	FString GetPluginDir() const;
	
private:
	bool LoadLibrary_libavstream();
	void* Handle_libavstream = nullptr;

	TUniquePtr<avs::Context> Context;
	static void LogMessageHandler(avs::LogSeverity Severity, const char* Msg, void*);
};
