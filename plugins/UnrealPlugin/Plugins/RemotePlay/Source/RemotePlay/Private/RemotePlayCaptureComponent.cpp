// Copyright 2018 Simul.co

#include "RemotePlayCaptureComponent.h"
#include "RemotePlayEncodePipeline.h"
#include "RemotePlayNetworkPipeline.h"
#include "RemotePlayModule.h"

#include "Engine.h"
#include "Engine/GameViewportClient.h"

#include "GameFramework/Actor.h"

struct FCaptureContext
{
	TUniquePtr<FRemotePlayEncodePipeline> EncodePipeline;
	TUniquePtr<FRemotePlayNetworkPipeline> NetworkPipeline;
	avs::Queue EncodeToNetworkQueue;
};

URemotePlayCaptureComponent::URemotePlayCaptureComponent()
	: CaptureContext(new FCaptureContext)
	, bRenderOwner(false)
	, bIsStreaming(false)
{
	PrimaryComponentTick.bCanEverTick = true;
	bCaptureEveryFrame = false;
	bCaptureOnMovement = false;

	EncodeParams.FrameWidth   = 3840;
	EncodeParams.FrameHeight  = 1920;
	EncodeParams.IDRInterval  = 120;
	EncodeParams.TargetFPS    = 60;
	EncodeParams.bDeferOutput = true;
}

URemotePlayCaptureComponent::~URemotePlayCaptureComponent()
{
	delete CaptureContext;
}
	
void URemotePlayCaptureComponent::BeginPlay()
{
	Super::BeginPlay();
	CaptureContext->EncodeToNetworkQueue.configure(16);

	if(!bRenderOwner)
	{
		AActor* OwnerActor = GetTypedOuter<AActor>();
		check(OwnerActor);

		TArray<AActor*> OwnerAttachedActors;
		OwnerActor->GetAttachedActors(OwnerAttachedActors);
		HiddenActors.Add(OwnerActor);
		HiddenActors.Append(OwnerAttachedActors);
	}
}
	
void URemotePlayCaptureComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if(bIsStreaming)
	{
		StopStreaming();
	}
	CaptureContext->EncodeToNetworkQueue.deconfigure();

	Super::EndPlay(Reason);
}
	
void URemotePlayCaptureComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	UActorComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);
	
	if(bIsStreaming)
	{
		CaptureSceneDeferred();

		if(CaptureContext->NetworkPipeline.IsValid())
		{
			CaptureContext->NetworkPipeline->Process();
		}
	}
}
	
void URemotePlayCaptureComponent::StartStreaming(const FString& RemoteIP, int32 RemotePort)
{
	if(bIsStreaming)
	{
		UE_LOG(LogRemotePlay, Warning, TEXT("Capture: Already streaming"));
		return;
	}

	if(!ViewportDrawnDelegateHandle.IsValid())
	{
		if(UGameViewportClient* GameViewport = GEngine->GameViewport)
		{
			ViewportDrawnDelegateHandle = GameViewport->OnDrawn().AddUObject(this, &URemotePlayCaptureComponent::OnViewportDrawn);
		}
	}

	if(!CaptureContext->NetworkPipeline.IsValid())
	{
		FRemotePlayNetworkParameters NetworkParams;
		NetworkParams.RemoteIP   = RemoteIP;
		NetworkParams.RemotePort = RemotePort;
		NetworkParams.LocalPort  = NetworkParams.RemotePort;

		CaptureContext->NetworkPipeline.Reset(new FRemotePlayNetworkPipeline(NetworkParams, CaptureContext->EncodeToNetworkQueue));
		CaptureContext->NetworkPipeline->Initialize();
	}

	bIsStreaming = true;
	UE_LOG(LogRemotePlay, Log, TEXT("Capture: Started streaming to %s:%d"), *RemoteIP, RemotePort);
}

void URemotePlayCaptureComponent::StopStreaming()
{
	if(!bIsStreaming)
	{
		UE_LOG(LogRemotePlay, Warning, TEXT("Capture: Was not streaming"));
		return;
	}

	if(ViewportDrawnDelegateHandle.IsValid())
	{
		if(UGameViewportClient* GameViewport = GEngine->GameViewport)
		{
			GameViewport->OnDrawn().Remove(ViewportDrawnDelegateHandle);
		}
		ViewportDrawnDelegateHandle.Reset();
	}
	
	if(CaptureContext->EncodePipeline.IsValid())
	{
		CaptureContext->EncodePipeline->Release();
		CaptureContext->EncodePipeline.Reset();
	}
	if(CaptureContext->NetworkPipeline.IsValid())
	{
		CaptureContext->NetworkPipeline->Release();
		CaptureContext->NetworkPipeline.Reset();
	}

	bIsStreaming = false;
	UE_LOG(LogRemotePlay, Log, TEXT("Capture: Stopped streaming"));
}

void URemotePlayCaptureComponent::OnViewportDrawn()
{
	if(TextureTarget)
	{
		if(!CaptureContext->EncodePipeline.IsValid())
		{
			CaptureContext->EncodePipeline.Reset(new FRemotePlayEncodePipeline(EncodeParams, CaptureContext->EncodeToNetworkQueue));
			CaptureContext->EncodePipeline->Initialize();
		}
		CaptureContext->EncodePipeline->EncodeFrame(GetWorld()->Scene, TextureTarget);
	}
}
