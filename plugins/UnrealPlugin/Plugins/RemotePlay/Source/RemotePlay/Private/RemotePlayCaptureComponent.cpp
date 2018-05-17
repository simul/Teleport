// Copyright 2018 Simul.co

#include "RemotePlayCaptureComponent.h"
#include "RemotePlayEncodePipeline.h"

#include "Engine.h"
#include "Engine/GameViewportClient.h"

#include "GameFramework/Actor.h"

struct FCaptureContext
{
	TUniquePtr<FRemotePlayEncodePipeline> EncodePipeline;
	avs::Queue EncodeToNetworkQueue;
};

URemotePlayCaptureComponent::URemotePlayCaptureComponent()
	: CaptureContext(new FCaptureContext)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	bCaptureEveryFrame = true;
	bWantsInitializeComponent = true;

	EncodeParams.FrameWidth   = 2048;
	EncodeParams.FrameHeight  = 1024;
	EncodeParams.IDRFrequency = 60;

	NetworkParams.LocalPort  = 1666;
	NetworkParams.RemoteIP   = TEXT("127.0.0.1");
	NetworkParams.RemotePort = NetworkParams.LocalPort + 1;
}

URemotePlayCaptureComponent::~URemotePlayCaptureComponent()
{
	delete CaptureContext;
}
	
void URemotePlayCaptureComponent::InitializeComponent()
{
	Super::InitializeComponent();
	CaptureContext->EncodeToNetworkQueue.configure(16);
}

void URemotePlayCaptureComponent::UninitializeComponent()
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
	CaptureContext->EncodeToNetworkQueue.deconfigure();

	Super::UninitializeComponent();
}

void URemotePlayCaptureComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	UActorComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if(bCaptureEveryFrame)
	{
		CaptureSceneDeferred();
		if(!ViewportDrawnDelegateHandle.IsValid())
		{
			if(UGameViewportClient* GameViewport = GEngine->GameViewport)
			{
				ViewportDrawnDelegateHandle = GameViewport->OnDrawn().AddUObject(this, &URemotePlayCaptureComponent::OnViewportDrawn);
			}
		}
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
