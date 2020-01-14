// Copyright 2018 Simul.co

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneCaptureComponentCube.h"

#include "SimulCasterServer/CaptureComponent.h"

#include "UnrealCasterSettings.h"

#include "RemotePlayCaptureComponent.generated.h"

//Unreal Header Tool doesn't seem to like multiple inheritance with namespaces, so here is a typedef of the namespaced class.
typedef SCServer::CaptureComponent SCServer_CaptureComponent;

//! This component is added to the player pawn. Derived from the SceneCaptureCube component, it
//! continuously captures the surrounding image around the Pawn. However, it has
//! other responsibilities as well.
UCLASS(hidecategories = (Collision, Object, Physics, SceneComponent), meta = (BlueprintSpawnableComponent))
class REMOTEPLAY_API URemotePlayCaptureComponent : public USceneCaptureComponentCube, public SCServer_CaptureComponent
{
	GENERATED_BODY()
public:
	URemotePlayCaptureComponent();
	virtual ~URemotePlayCaptureComponent() = default;

	/* Begin UActorComponent interface */
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	/* End UActorComponent interface */

	/// SCServer::CaptureComponent Start
	virtual void startStreaming(SCServer::CasterContext* context) override;
	virtual void stopStreaming() override;

	virtual void requestKeyframe() override;

	virtual SCServer::CameraInfo& getClientCameraInfo() override;
	/// SCServer::CaptureComponent End

	bool ShouldRenderFace(int32 FaceId) const override;

	void UpdateSceneCaptureContents(FSceneInterface* Scene) override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = RemotePlay)
	FUnrealCasterEncoderSettings EncodeParams;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = RemotePlay)
	uint32 bRenderOwner : 1;

	const FUnrealCasterEncoderSettings& GetEncoderSettings();
private: 
	struct FQuad
	{
		FVector BottomLeft;
		FVector TopLeft;
		FVector BottomRight;
		FVector TopRight;
	};

	void OnViewportDrawn();
	FDelegateHandle ViewportDrawnDelegateHandle;
	void CullHiddenCubeSegments();
	static void CreateCubeQuads(TArray<FQuad>& Quads, uint32 BlocksPerFaceAcross, float CubeWidth);
	static bool VectorIntersectsFrustum(const FVector& Vector, const FMatrix& ViewProjection);

	SCServer::CameraInfo ClientCamInfo;

	TArray<FQuad> CubeQuads;
	TArray<bool> QuadsToRender;
	TArray<bool> FacesToRender;

	struct FUnrealCasterContext* UnrealCasterContext;
	class URemotePlayReflectionCaptureComponent *RemotePlayReflectionCaptureComponent;
	bool bIsStreaming;
	bool bSendKeyframe;
};