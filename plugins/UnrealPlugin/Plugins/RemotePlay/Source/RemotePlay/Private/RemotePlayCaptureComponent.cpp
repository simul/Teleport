// Copyright 2018 Simul.co

#include "RemotePlayCaptureComponent.h"
#include "RemotePlayEncodePipeline.h"
#include "RemotePlayNetworkPipeline.h"

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
{
	PrimaryComponentTick.bCanEverTick = true;
	bCaptureEveryFrame = true;

	EncodeParams.FrameWidth   = 2048;
	EncodeParams.FrameHeight  = 1024;
	EncodeParams.IDRInterval  = 60;
	EncodeParams.TargetFPS    = 60;

	NetworkParams.LocalPort  = 1666;
	NetworkParams.RemoteIP   = TEXT("127.0.0.1");
	NetworkParams.RemotePort = NetworkParams.LocalPort + 1;
}

URemotePlayCaptureComponent::~URemotePlayCaptureComponent()
{
	delete CaptureContext;
}
	
void URemotePlayCaptureComponent::BeginPlay()
{
	Super::BeginPlay();
	CaptureContext->EncodeToNetworkQueue.configure(16);
}
	
void URemotePlayCaptureComponent::EndPlay(const EEndPlayReason::Type Reason)
{
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
	CaptureContext->EncodeToNetworkQueue.deconfigure();

	Super::EndPlay(Reason);
}
	
void URemotePlayCaptureComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	UActorComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);
	
	if(HasBegunPlay() && bCaptureEveryFrame)
	{
		CaptureSceneDeferred();

		if(!ViewportDrawnDelegateHandle.IsValid())
		{
			if(UGameViewportClient* GameViewport = GEngine->GameViewport)
			{
				ViewportDrawnDelegateHandle = GameViewport->OnDrawn().AddUObject(this, &URemotePlayCaptureComponent::OnViewportDrawn);
			}
		}

		if(!CaptureContext->NetworkPipeline.IsValid())
		{
			CaptureContext->NetworkPipeline.Reset(new FRemotePlayNetworkPipeline(NetworkParams, CaptureContext->EncodeToNetworkQueue));
			CaptureContext->NetworkPipeline->Initialize();
		}
		CaptureContext->NetworkPipeline->Process();
	}
}

void URemotePlayCaptureComponent::OnViewportDrawn()
{
	if(bCaptureEveryFrame && TextureTarget)
	{
		if(!CaptureContext->EncodePipeline.IsValid())
		{
			CaptureContext->EncodePipeline.Reset(new FRemotePlayEncodePipeline(EncodeParams, CaptureContext->EncodeToNetworkQueue));
			CaptureContext->EncodePipeline->Initialize();
		}
		CaptureContext->EncodePipeline->EncodeFrame(GetWorld()->Scene, TextureTarget);
	}
}
