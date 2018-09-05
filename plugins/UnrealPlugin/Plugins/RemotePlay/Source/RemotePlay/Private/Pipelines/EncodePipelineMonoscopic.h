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
	virtual void Initialize(const FRemotePlayEncodeParameters& InParams, avs::Queue& InOutputQueue) override;
	virtual void Release() override;
	virtual void EncodeFrame(FSceneInterface* InScene, UTexture* InSourceTexture) override;
	/* End IEncodePipeline interface */

private:
	void Initialize_RenderThread(FRHICommandListImmediate& RHICmdList);
	void Release_RenderThread(FRHICommandListImmediate& RHICmdList);
	void PrepareFrame_RenderThread(FRHICommandListImmediate& RHICmdList, FTextureRenderTargetResource* TargetResource, ERHIFeatureLevel::Type FeatureLevel);
	void EncodeFrame_RenderThread(FRHICommandListImmediate& RHICmdList);

	FRemotePlayEncodeParameters Params;
	FTexture2DRHIRef InputSurfaceTexture;
	FUnorderedAccessViewRHIRef InputSurfaceUAV;

	avs::Pipeline Pipeline;
	avs::Encoder Encoder;
	avs::Surface InputSurface;
	avs::Queue*  OutputQueue = nullptr;
};
