// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "UObject/ObjectMacros.h"
#include "Components/BoxReflectionCaptureComponent.h"
#include "RemotePlayReflectionCaptureComponent.generated.h"

struct FCubeTexture
{
	FTextureCubeRHIRef TextureCubeRHIRef;
	FUnorderedAccessViewRHIRef UnorderedAccessViewRHIRefs[8];
	FShaderResourceViewRHIRef TextureCubeMipRHIRefs[8];
};

UCLASS(hidecategories = (Collision, Object, Physics, SceneComponent), meta = (BlueprintSpawnableComponent))
class URemotePlayReflectionCaptureComponent : public UReflectionCaptureComponent
{
	GENERATED_UCLASS_BODY()

	/** Adjust capture transition distance */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=ReflectionCapture, meta=(UIMin = "1", UIMax = "1000"))
	float BoxTransitionDistance;

	UPROPERTY()
	class UBoxComponent* PreviewInfluenceBox;

	UPROPERTY()
	class UBoxComponent* PreviewCaptureBox;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SceneCapture)
	class UTextureRenderTargetCube* OverrideTexture;

public:
	virtual void UpdatePreviewShape() override;
	virtual float GetInfluenceBoundingRadius() const override;
	void Initialize();
	void UpdateContents(FScene *Scene, class UTextureRenderTargetCube *src, ERHIFeatureLevel::Type FeatureLevel) ;

	void PrepareFrame(FScene *Scene, struct FSurfaceTexture *UAV, ERHIFeatureLevel::Type FeatureLevel);

	// To test:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// Attached to a capture component?
	bool bAttached;
private:
	void Initialize_RenderThread(FRHICommandListImmediate& RHICmdList);
	void Release_RenderThread(FRHICommandListImmediate& RHICmdList);
	void UpdateReflections_RenderThread(FRHICommandListImmediate& RHICmdList, FScene *Scene, UTextureRenderTargetCube *InSourceTexture, ERHIFeatureLevel::Type FeatureLevel);
	void WriteReflections_RenderThread(FRHICommandListImmediate& RHICmdList, FScene *Scene, struct FSurfaceTexture *, ERHIFeatureLevel::Type FeatureLevel);

	void Decompose_RenderThread(FRHICommandListImmediate& RHICmdList
		, FCubeTexture &CubeTexture
		, FSurfaceTexture *TargetSurfaceTexture, FShader *Shader, FIntPoint TargetOffset);
	void Init(FRHICommandListImmediate& RHICmdList,FCubeTexture &t, int32 size, int32 mips);
	void Release(FCubeTexture &t);
	struct FShaderDirectionalLight
	{
		FLinearColor Color;
		FVector Direction;
	};

	FCubeTexture SpecularCubeTexture;
	FCubeTexture DiffuseCubeTexture;
	FCubeTexture LightingCubeTexture;
};

