// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "UObject/ObjectMacros.h"
#include "Components/BoxReflectionCaptureComponent.h"
#include "RemotePlayReflectionCaptureComponent.generated.h"

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

public:
	virtual void UpdatePreviewShape() override;
	virtual float GetInfluenceBoundingRadius() const override;
	void UpdateContents(class UTextureRenderTargetCube *src, ERHIFeatureLevel::Type FeatureLevel) ;

private:
	void Initialize_RenderThread(FRHICommandListImmediate& RHICmdList);
	void Release_RenderThread(FRHICommandListImmediate& RHICmdList);
	void UpdateReflections_RenderThread(FRHICommandListImmediate& RHICmdList, FTextureRenderTargetResource* TargetResource, ERHIFeatureLevel::Type FeatureLevel);
	template<typename ShaderType> void DispatchUpdateReflectionsShader(FRHICommandListImmediate& RHICmdList, FTextureRHIRef TextureRHI, ERHIFeatureLevel::Type FeatureLevel);
};

