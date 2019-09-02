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
	 void Initialize(const FRemotePlayEncodeParameters& InParams, avs::Queue* InColorQueue, avs::Queue* InDepthQueue) override;
	 void Release() override;
	void PrepareFrame(FSceneInterface* InScene, UTexture* InSourceTexture) override;
	 void EncodeFrame(FSceneInterface* InScene, UTexture* InSourceTexture) override;
	 FSurfaceTexture *GetSurfaceTexture() override
	 {
		 return &ColorSurfaceTexture;
	 }
	/* End IEncodePipeline interface */

private:
	void Initialize_RenderThread(FRHICommandListImmediate& RHICmdList);
	void Release_RenderThread(FRHICommandListImmediate& RHICmdList);
	void PrepareFrame_RenderThread(FRHICommandListImmediate& RHICmdList, FTextureRenderTargetResource* TargetResource, ERHIFeatureLevel::Type FeatureLevel);
	void EncodeFrame_RenderThread(FRHICommandListImmediate& RHICmdList);

	template<typename ShaderType>
	void DispatchProjectCubemapShader(FRHICommandListImmediate& RHICmdList, FTextureRHIRef TextureRHI, ERHIFeatureLevel::Type FeatureLevel);


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
