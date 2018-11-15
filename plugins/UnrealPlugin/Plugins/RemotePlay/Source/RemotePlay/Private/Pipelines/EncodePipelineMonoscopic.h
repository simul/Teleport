// Copyright 2018 Simul.co

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "EncodePipelineInterface.h"

class UTextureRenderTargetCube;
class FTextureRenderTargetResource;

class FEncodePipelineMonoscopic : public IEncodePipeline
{
public:
	/* Begin IEncodePipeline interface */
	virtual void Initialize(const FRemotePlayEncodeParameters& InParams, avs::Queue* InColorQueue, avs::Queue* InDepthQueue) override;
	virtual void Release() override;
	virtual void EncodeFrame(FSceneInterface* InScene, UTexture* InSourceTexture) override;
	/* End IEncodePipeline interface */

private:
	void Initialize_RenderThread(FRHICommandListImmediate& RHICmdList);
	void Release_RenderThread(FRHICommandListImmediate& RHICmdList);
	void PrepareFrame_RenderThread(FRHICommandListImmediate& RHICmdList, FTextureRenderTargetResource* TargetResource, ERHIFeatureLevel::Type FeatureLevel);
	void EncodeFrame_RenderThread(FRHICommandListImmediate& RHICmdList);

	struct FSurfaceTexture
	{
		FTexture2DRHIRef Texture;
		FUnorderedAccessViewRHIRef UAV;
	};

	FRemotePlayEncodeParameters Params;
	FSurfaceTexture ColorSurfaceTexture;
	FSurfaceTexture DepthSurfaceTexture;

	TUniquePtr<avs::Pipeline> Pipeline;
	TArray<avs::Encoder> Encoder;
	TArray<avs::Surface> InputSurface;
	avs::Queue* ColorQueue = nullptr;
	avs::Queue* DepthQueue = nullptr;

	FVector2D WorldZToDeviceZTransform;
};
