// Copyright 2018 Simul.co

#include "RemotePlayModule.h"
#include "RemotePlayPlatform.h"

#include "IPluginManager.h"
#include "PlatformProcess.h"

#define LOCTEXT_NAMESPACE "FRemotePlayModule"

void FRemotePlayModule::StartupModule()
{
	if(LoadLibrary_libavstream())
	{
		Context.Reset(new avs::Context);
		Context->setMessageHandler(FRemotePlayModule::LogMessageHandler, this);

		UE_LOG(LogRemotePlay, Log, TEXT("Runtime module initialized"));
	}
}

void FRemotePlayModule::ShutdownModule()
{
	if(Handle_libavstream)
	{
		UE_LOG(LogRemotePlay, Log, TEXT("Runtime module shutdown"));

		Context.Reset();

		FPlatformProcess::FreeDllHandle(Handle_libavstream);
		Handle_libavstream = nullptr;
	}
}
	
FString FRemotePlayModule::GetPluginDir() const
{
	static const FString PluginDir = IPluginManager::Get().FindPlugin("RemotePlay")->GetBaseDir();
	return PluginDir;
}
	
bool FRemotePlayModule::LoadLibrary_libavstream()
{
	check(!Handle_libavstream);

	const FString LibraryPath = GetPluginDir() / TEXT("Libraries/libavstream") / REMOTEPLAY_PLATFORM;
	const FString LibraryFile = LibraryPath / REMOTEPLAY_LIBAVSTREAM;

	FPlatformProcess::PushDllDirectory(*LibraryPath);
	Handle_libavstream = FPlatformProcess::GetDllHandle(*LibraryFile);
	FPlatformProcess::PopDllDirectory(*LibraryPath);

	if(Handle_libavstream)
	{
		UE_LOG(LogRemotePlay, Log, TEXT("libavstream library loaded"));
		return true;
	}
	else
	{
		UE_LOG(LogRemotePlay, Error, TEXT("Failed to load libavstream library"));
		return false;
	}
}
	
void FRemotePlayModule::LogMessageHandler(avs::LogSeverity Severity, const char* Msg, void*)
{
	switch(Severity)
	{
	case avs::LogSeverity::Critical:
		UE_LOG(LogRemotePlay, Fatal, TEXT("%s"), UTF8_TO_TCHAR(Msg));
		break;
	case avs::LogSeverity::Error:
		UE_LOG(LogRemotePlay, Error, TEXT("%s"), UTF8_TO_TCHAR(Msg));
		break;
	case avs::LogSeverity::Warning:
		UE_LOG(LogRemotePlay, Warning, TEXT("%s"), UTF8_TO_TCHAR(Msg));
		break;
	case avs::LogSeverity::Info:
	case avs::LogSeverity::Debug:
		UE_LOG(LogRemotePlay, Log, TEXT("%s"), UTF8_TO_TCHAR(Msg));
		break;
	default:
		check(0);
	}
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FRemotePlayModule, RemotePlay)
DEFINE_LOG_CATEGORY(LogRemotePlay);
