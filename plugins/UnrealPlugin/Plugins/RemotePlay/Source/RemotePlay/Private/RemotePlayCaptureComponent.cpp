// Copyright 2018 Simul.co

#include "RemotePlayCaptureComponent.h"
#include "RemotePlayModule.h"
#include "Pipelines/EncodePipelineMonoscopic.h"
#include "Pipelines/NetworkPipeline.h"

#include "Engine.h"
#include "Engine/GameViewportClient.h"

#include "GameFramework/Actor.h"

struct FCaptureContext
{
	TUniquePtr<IEncodePipeline> EncodePipeline;
	TUniquePtr<FNetworkPipeline> NetworkPipeline;
	TUniquePtr<avs::Queue> ColorQueue;
	TUniquePtr<avs::Queue> DepthQueue;
	bool bCaptureDepth = false;
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

	CaptureContext->ColorQueue.Reset(new avs::Queue);
	CaptureContext->ColorQueue->configure(16);

	if(CaptureSource == ESceneCaptureSource::SCS_SceneColorSceneDepth)
	{
		CaptureContext->bCaptureDepth = true;
		CaptureContext->DepthQueue.Reset(new avs::Queue);
		CaptureContext->DepthQueue->configure(16);
	}
	else
	{
		CaptureContext->bCaptureDepth = false;
	}

	if(!bRenderOwner)
	{
		AActor* OwnerActor = GetTypedOuter<AActor>();
		check(OwnerActor);

		TArray<AActor*> OwnerAttachedActors;
		OwnerActor->GetAttachedActors(OwnerAttachedActors);
		HiddenActors.Add(OwnerActor);
		HiddenActors.Append(OwnerAttachedActors);
	}

	StartStreaming(TEXT("127.0.0.1"), 1667);
}
	
void URemotePlayCaptureComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if(bIsStreaming)
	{
		StopStreaming();
	}

	CaptureContext->ColorQueue.Reset();
	CaptureContext->DepthQueue.Reset();

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

		CaptureContext->NetworkPipeline.Reset(new FNetworkPipeline);
		CaptureContext->NetworkPipeline->Initialize(NetworkParams, CaptureContext->ColorQueue.Get(), CaptureContext->DepthQueue.Get());
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
			CaptureContext->EncodePipeline.Reset(new FEncodePipelineMonoscopic);
			CaptureContext->EncodePipeline->Initialize(EncodeParams, CaptureContext->ColorQueue.Get(), CaptureContext->DepthQueue.Get());
		}
		CaptureContext->EncodePipeline->EncodeFrame(GetWorld()->Scene, TextureTarget);
	}
}
