// (c) 2018 Simul.co

#include "RemotePlayCaptureComponent.h"
#include "RemotePlayPlugin.h"

#include "Engine.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
	
URemotePlayCaptureComponent::URemotePlayCaptureComponent()
{
	bWantsInitializeComponent = true;
	bCaptureEveryFrame = true;
}
	
void URemotePlayCaptureComponent::InitializeComponent()
{
	Super::InitializeComponent();
	if(UGameViewportClient* GameViewport = GEngine->GameViewport)
	{
		ViewportDrawnDelegateHandle = GameViewport->OnDrawn().AddUObject(this, &URemotePlayCaptureComponent::OnViewportDrawn);
	}
}
	
void URemotePlayCaptureComponent::UninitializeComponent()
{
	if(UGameViewportClient* GameViewport = GEngine->GameViewport)
	{
		GameViewport->OnDrawn().Remove(ViewportDrawnDelegateHandle);
	}
	Super::UninitializeComponent();
}

void URemotePlayCaptureComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	UActorComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if(GetWorld()->IsServer() && bCaptureEveryFrame)
	{
		CaptureSceneDeferred();
	}
}

void URemotePlayCaptureComponent::OnViewportDrawn()
{
	
}