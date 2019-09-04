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
#include "RemotePlayMonitor.h"
#include "RemotePlayReflectionCaptureComponent.h"

URemotePlayCaptureComponent::URemotePlayCaptureComponent()
	: bRenderOwner(false)
	, RemotePlayContext(nullptr)
	, RemotePlayReflectionCaptureComponent(nullptr)
	, bIsStreaming(false)
{
	PrimaryComponentTick.bCanEverTick = true;
	bCaptureEveryFrame = false;
	bCaptureOnMovement = false; 

	EncodeParams.FrameWidth = 3840;
	EncodeParams.FrameHeight = 1920;

	EncodeParams.DepthWidth = 1920;
	EncodeParams.DepthHeight = 960;

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
	AActor* OwnerActor = GetTypedOuter<AActor>();
	if (bRenderOwner)
	{
		RemotePlayReflectionCaptureComponent = Cast<URemotePlayReflectionCaptureComponent>(OwnerActor->GetComponentByClass(URemotePlayReflectionCaptureComponent::StaticClass()));
	}
	else
	{
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

void URemotePlayCaptureComponent::UpdateSceneCaptureContents(FSceneInterface* Scene)
{
	if (bIsStreaming && RemotePlayContext != nullptr && RemotePlayContext->EncodePipeline.IsValid())
	{
		FTransform Transform = GetComponentTransform();
		RemotePlayContext->EncodePipeline->AddCameraTransform(Transform);
	}
	Super::UpdateSceneCaptureContents(Scene);
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
			RemotePlayContext->EncodePipeline->Initialize(EncodeParams, RemotePlayContext,RemotePlayContext->ColorQueue.Get(), RemotePlayContext->DepthQueue.Get());
			
			if (RemotePlayReflectionCaptureComponent)
			{
				RemotePlayReflectionCaptureComponent->Initialize();
				RemotePlayReflectionCaptureComponent->bAttached = true;
			}
		}

		ARemotePlayMonitor *Monitor = ARemotePlayMonitor::Instantiate(GetWorld());

		if (Monitor&&Monitor->VideoEncodeFrequency > 1)
		{
			static int u = 1;
			u--;
			if (!u)
				u = Monitor->VideoEncodeFrequency;
			else
				return; 
		}
		RemotePlayContext->EncodePipeline->PrepareFrame(GetWorld()->Scene, TextureTarget);
		if (RemotePlayReflectionCaptureComponent)
		{
			RemotePlayReflectionCaptureComponent->UpdateContents(
				GetWorld()->Scene->GetRenderScene()
				, TextureTarget
				, GetWorld()->Scene->GetFeatureLevel());
			RemotePlayReflectionCaptureComponent->PrepareFrame(
				GetWorld()->Scene->GetRenderScene()
				, RemotePlayContext->EncodePipeline->GetSurfaceTexture()
				, GetWorld()->Scene->GetFeatureLevel());
		}
		RemotePlayContext->EncodePipeline->EncodeFrame(GetWorld()->Scene, TextureTarget);
	}
}

FTransform URemotePlayCaptureComponent::GetToWorldTransform()
{
	return GetComponentTransform();
}
