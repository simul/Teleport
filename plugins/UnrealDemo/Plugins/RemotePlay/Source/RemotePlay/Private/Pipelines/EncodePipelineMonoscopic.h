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
	void Initialize(const FRemotePlayEncodeParameters& InParams, struct FRemotePlayContext *context, ARemotePlayMonitor* InMonitor, avs::Queue* InColorQueue, avs::Queue* InDepthQueue) override;
	void Release() override;
	void PrepareFrame(FSceneInterface* InScene, UTexture* InSourceTexture) override;
	void EncodeFrame(FSceneInterface* InScene, UTexture* InSourceTexture, FTransform& CameraTransform) override;
	FSurfaceTexture *GetSurfaceTexture() override
	{
		return &ColorSurfaceTexture;
	}
	/* End IEncodePipeline interface */

private:
	void Initialize_RenderThread(FRHICommandListImmediate& RHICmdList);
	void Release_RenderThread(FRHICommandListImmediate& RHICmdList);
	void PrepareFrame_RenderThread(FRHICommandListImmediate& RHICmdList, FTextureRenderTargetResource* TargetResource, ERHIFeatureLevel::Type FeatureLevel);
	void EncodeFrame_RenderThread(FRHICommandListImmediate& RHICmdList, FTransform CameraTransform);

	template<typename ShaderType>
	void DispatchProjectCubemapShader(FRHICommandListImmediate& RHICmdList, FTextureRHIRef TextureRHI, FUnorderedAccessViewRHIRef TextureUAVRHI, ERHIFeatureLevel::Type FeatureLevel);

	void DispatchDecomposeCubemapShader(FRHICommandListImmediate& RHICmdList, FTextureRHIRef TextureRHI, FUnorderedAccessViewRHIRef TextureUAVRHI, ERHIFeatureLevel::Type FeatureLevel);
	
	struct FRemotePlayContext* RemotePlayContext;

	FRemotePlayEncodeParameters Params;
	FSurfaceTexture ColorSurfaceTexture;
	FSurfaceTexture DepthSurfaceTexture;

	TUniquePtr<avs::Pipeline> Pipeline;
	TArray<avs::Encoder> Encoders;
	TArray<avs::Surface> InputSurfaces;
	avs::Queue* ColorQueue = nullptr;
	avs::Queue* DepthQueue = nullptr;

	FVector2D WorldZToDeviceZTransform;

	FTextureRHIRef				SourceCubemapRHI;
	FUnorderedAccessViewRHIRef UnorderedAccessViewRHIRef;

	FTextureCubeRHIRef LightingTextureCubeRHIRef;
	FUnorderedAccessViewRHIRef LightingUnorderedAccessViewRHIRefs[8];

	ARemotePlayMonitor* Monitor;
};
