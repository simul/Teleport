// Copyright 2018 Simul.co
#define WIN32_LEAN_AND_MEAN

#include "Components/CaptureComponent.h"
#include "RemotePlayModule.h"
#include "Pipelines/EncodePipelineMonoscopic.h"
#include "Pipelines/NetworkPipeline.h"

#include "Engine.h"
#include "Engine/GameViewportClient.h"

#include "GameFramework/Actor.h"
#include "RemotePlaySettings.h"


URemotePlayCaptureComponent::URemotePlayCaptureComponent()
	: bRenderOwner(false)
	, RemotePlayContext(nullptr)
	, bIsStreaming(false)
{
	PrimaryComponentTick.bCanEverTick = true;
	bCaptureEveryFrame = false;
	bCaptureOnMovement = false;

	EncodeParams.FrameWidth = 3840;
	EncodeParams.FrameHeight = 1920;
	EncodeParams.IDRInterval = 120;
	EncodeParams.TargetFPS = 60; 
	EncodeParams.bDeferOutput = true;
	EncodeParams.bLinearDepth = true;
	EncodeParams.bWriteDepthTexture = false;
	EncodeParams.bStackDepth = true;
	EncodeParams.MaxDepth = 10000.0f; 
}

URemotePlayCaptureComponent::~URemotePlayCaptureComponent()
{
}

void URemotePlayCaptureComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!bRenderOwner)
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
	Super::EndPlay(Reason);
}

void URemotePlayCaptureComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

void URemotePlayCaptureComponent::StartStreaming(FRemotePlayContext *Context)
{
	RemotePlayContext = Context;

	if (!ViewportDrawnDelegateHandle.IsValid())
	{
		if (UGameViewportClient* GameViewport = GEngine->GameViewport)
		{
			ViewportDrawnDelegateHandle = GameViewport->OnDrawn().AddUObject(this, &URemotePlayCaptureComponent::OnViewportDrawn);
		}
	}

	bIsStreaming = true;
	bCaptureEveryFrame = true;
}

void URemotePlayCaptureComponent::StopStreaming()
{
	if (!RemotePlayContext)
	{
		UE_LOG(LogRemotePlay, Warning, TEXT("Capture: null RemotePlayContext"));
		return;
	}

	if (ViewportDrawnDelegateHandle.IsValid())
	{
		if (UGameViewportClient* GameViewport = GEngine->GameViewport)
		{
			GameViewport->OnDrawn().Remove(ViewportDrawnDelegateHandle);
		}
		ViewportDrawnDelegateHandle.Reset();
	}

	RemotePlayContext = nullptr;
	bCaptureEveryFrame = false;
	UE_LOG(LogRemotePlay, Log, TEXT("Capture: Stopped streaming"));
}

void URemotePlayCaptureComponent::OnViewportDrawn()
{
	if (TextureTarget)
	{
		if (!RemotePlayContext->EncodePipeline.IsValid())
		{
			RemotePlayContext->EncodePipeline.Reset(new FEncodePipelineMonoscopic);
			RemotePlayContext->EncodePipeline->Initialize(EncodeParams, RemotePlayContext->ColorQueue.Get(), RemotePlayContext->DepthQueue.Get());
		}

		const URemotePlaySettings *RemotePlaySettings = GetDefault<URemotePlaySettings>();


		if (RemotePlaySettings&&RemotePlaySettings->VideoEncodeFrequency > 1)
		{
			static int u = 1;
			u--;
			if (!u)
				u = RemotePlaySettings->VideoEncodeFrequency;
			else
				return; 
		}
		RemotePlayContext->EncodePipeline->EncodeFrame(GetWorld()->Scene, TextureTarget);
	}
}
