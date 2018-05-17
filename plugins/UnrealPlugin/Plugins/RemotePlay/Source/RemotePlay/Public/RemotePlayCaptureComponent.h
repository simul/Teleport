// Copyright 2018 Simul.co

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneCaptureComponentCube.h"
#include "RemotePlayParameters.h"
#include "RemotePlayCaptureComponent.generated.h"

UCLASS(hidecategories=(Collision, Object, Physics, SceneComponent), meta=(BlueprintSpawnableComponent))
class REMOTEPLAY_API URemotePlayCaptureComponent : public USceneCaptureComponentCube
{
	GENERATED_BODY()
public:	
	URemotePlayCaptureComponent();
	~URemotePlayCaptureComponent();

	/* Begin UActorComponent interface */
	virtual void InitializeComponent() override;
	virtual void UninitializeComponent() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	/* End UActorComponent interface */

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category=RemotePlay)
	FRemotePlayEncodeParameters EncodeParams;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category=RemotePlay)
	FRemotePlayNetworkParameters NetworkParams;

private:
	void OnViewportDrawn();
	FDelegateHandle ViewportDrawnDelegateHandle;

	struct FCaptureContext* CaptureContext;
};
