// Copyright 2018 Simul.co

#pragma once

#include "CoreMinimal.h"
#include "RemotePlay.h"
#include "GeometrySource.h"

#include "libavstream/libavstream.hpp"

DECLARE_LOG_CATEGORY_EXTERN(LogRemotePlay, Log, All);

#if !UE_BUILD_SHIPPING || !UE_BUILD_TEST
#define WITH_REMOTEPLAY_STATS 1
#endif

class FRemotePlayModule : public IRemotePlay
{
public:
	/* Begin IModuleInterface */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	/* End IModuleInterface */
	
	FString GetPluginDir() const;
	GeometrySource* GetGeometrySource();
	static void LogCallback(const char *txt);
private:
	bool LoadLibrary_libavstream();
	void* Handle_libavstream = nullptr;

	TUniquePtr<avs::Context> Context;
	static void LogMessageHandler(avs::LogSeverity Severity, const char* Msg, void*);

	// ONE Geometry source covering all of the geometry.
	GeometrySource geometrySource;
#ifdef DISABLE
#endif
};
