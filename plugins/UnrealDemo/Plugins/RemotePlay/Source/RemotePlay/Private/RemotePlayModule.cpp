// Copyright 2018 Simul.co
#define WIN32_LEAN_AND_MEAN

#include "RemotePlayModule.h"
#include "RemotePlayPlatform.h"

#include "IPluginManager.h"
#include "PlatformProcess.h"
#include "ShaderCore.h"
#include "Misc/Paths.h"

#include "GameFramework/PlayerController.h"

#include "enet/enet.h"

#define LOCTEXT_NAMESPACE "FRemotePlayModule"


void FRemotePlayModule::StartupModule()
{
	// Dynamically load libavstream.dll.
	if(LoadLibrary_libavstream())
	{
		// We are currently linking statically, so this was not needed.
	}
	// Assuming successful, create a new avs::Context, which handles passing log messages back from avstream to Unreal.

	Context.Reset(new avs::Context);
	Context->setMessageHandler(FRemotePlayModule::LogMessageHandler, this);
	// enet is a library that provides reliable, in-order delivery of UDP packets.
	if(enet_initialize() != 0)
	{
		UE_LOG(LogRemotePlay, Error, TEXT("Failed to initialize ENET library"));
	}

	const FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("RemotePlay"))->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/Plugin/RemotePlay"), PluginShaderDir);
		
	UE_LOG(LogRemotePlay, Log, TEXT("Runtime module initialized"));
}

void FRemotePlayModule::ShutdownModule()
{
	UE_LOG(LogRemotePlay, Log, TEXT("Runtime module shutdown"));

	enet_deinitialize();

	if(Handle_libavstream)
	{
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

GeometrySource *FRemotePlayModule::GetGeometrySource()
{
	return &geometrySource;
}
	
bool FRemotePlayModule::LoadLibrary_libavstream()
{
	check(!Handle_libavstream);

	const FString LibraryPath = GetPluginDir() / TEXT("Libraries/libavstream/Release");// / REMOTEPLAY_PLATFORM;
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
	FString errstr;
	FString outstr;
	switch(Severity)
	{
	case avs::LogSeverity::Critical:
	case avs::LogSeverity::Error:
	case avs::LogSeverity::Warning:
		while (*Msg)
		{
			char c = *Msg;
			errstr += c;// *(UTF8_TO_TCHAR(&c));
			if (c == '\n')
			{
				UE_LOG(LogRemotePlay, Warning, TEXT("%s"), *errstr);
				errstr.Empty();
			}
			Msg++;
		}
		break;
	case avs::LogSeverity::Info:
	case avs::LogSeverity::Debug:
		while (*Msg)
		{
			char c = *Msg;
			outstr += UTF8_TO_TCHAR(c);
			if (c == '\n')
			{
				UE_LOG(LogRemotePlay, Warning, TEXT("%s"), *outstr);
				outstr.Empty();
			}
			Msg++;
		}
		break;
	default:
		check(0);
	}
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FRemotePlayModule, RemotePlay)
DEFINE_LOG_CATEGORY(LogRemotePlay);
