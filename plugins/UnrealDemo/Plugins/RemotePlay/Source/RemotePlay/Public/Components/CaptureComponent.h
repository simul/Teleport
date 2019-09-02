// Copyright 2018 Simul.co

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneCaptureComponentCube.h"
#include "RemotePlayParameters.h"
#include "Pipelines/EncodePipelineInterface.h"
#include "Pipelines/NetworkPipeline.h"
#include "RemotePlayContext.h"
#include "CaptureComponent.generated.h"

//! This component is added to the player pawn. Derived from the SceneCaptureCube component, it
//! continuously captures the surrounding image around the Pawn. However, it has
//! other responsibilities as well.
UCLASS(hidecategories = (Collision, Object, Physics, SceneComponent), meta = (BlueprintSpawnableComponent))
class REMOTEPLAY_API URemotePlayCaptureComponent : public USceneCaptureComponentCube
{
	GENERATED_BODY()
public:
	URemotePlayCaptureComponent();
	~URemotePlayCaptureComponent();

	/* Begin UActorComponent interface */
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	/* End UActorComponent interface */

	void UpdateSceneCaptureContents(FSceneInterface* Scene) override;

	void StartStreaming(FRemotePlayContext *Context);

	void StopStreaming();
	
	FTransform GetToWorldTransform();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = RemotePlay)
	FRemotePlayEncodeParameters EncodeParams;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = RemotePlay)
	uint32 bRenderOwner : 1;

private: 
	void OnViewportDrawn();
	FDelegateHandle ViewportDrawnDelegateHandle;

	struct FRemotePlayContext* RemotePlayContext;
	class URemotePlayReflectionCaptureComponent *RemotePlayReflectionCaptureComponent;
	bool bIsStreaming;
};
