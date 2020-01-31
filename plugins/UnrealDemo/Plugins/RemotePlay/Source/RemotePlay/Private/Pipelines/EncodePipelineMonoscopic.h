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
	void Initialise(const FUnrealCasterEncoderSettings& InSettings, struct SCServer::CasterContext* context, ARemotePlayMonitor* InMonitor) override;
	void Release() override;
	void CullHiddenCubeSegments(FSceneInterface* InScene, SCServer::CameraInfo& CameraInfo, int32 FaceSize, uint32 Divisor) override;
	void PrepareFrame(FSceneInterface* InScene, UTexture* InSourceTexture, FTransform& CameraTransform, const TArray<bool>& BlockIntersectionFlags) override;
	void EncodeFrame(FSceneInterface* InScene, UTexture* InSourceTexture, FTransform& CameraTransform, bool forceIDR) override;
	FSurfaceTexture *GetSurfaceTexture() override
	{
		return &ColorSurfaceTexture;
	}
	/* End IEncodePipeline interface */

private:
	void Initialize_RenderThread(FRHICommandListImmediate& RHICmdList);
	void Release_RenderThread(FRHICommandListImmediate& RHICmdList);
	void CullHiddenCubeSegments_RenderThread(FRHICommandListImmediate& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, SCServer::CameraInfo CameraInfo, int32 FaceSize, uint32 Divisor);
	void PrepareFrame_RenderThread(FRHICommandListImmediate& RHICmdList, FTextureRenderTargetResource* TargetResource, ERHIFeatureLevel::Type FeatureLevel, FVector CameraPosition, TArray<bool> BlockIntersectionFlags);
	void EncodeFrame_RenderThread(FRHICommandListImmediate& RHICmdList, FTransform CameraTransform, bool forceIDR);

	template<typename ShaderType>
	void DispatchProjectCubemapShader(FRHICommandListImmediate& RHICmdList, FTextureRHIRef TextureRHI, FUnorderedAccessViewRHIRef TextureUAVRHI, ERHIFeatureLevel::Type FeatureLevel);

	void DispatchDecomposeCubemapShader(FRHICommandListImmediate& RHICmdList, FTextureRHIRef TextureRHI, FUnorderedAccessViewRHIRef TextureUAVRHI, ERHIFeatureLevel::Type FeatureLevel,FVector CameraPosition, const TArray<bool>& BlockIntersectionFlags);
	
	struct FShaderFlag
	{
		uint32 flag = 0;
		uint32 pad0, pad1, pad2 = 0;
	};

	struct SCServer::CasterContext* CasterContext;

	FUnrealCasterEncoderSettings Settings;
	FSurfaceTexture ColorSurfaceTexture;
	FSurfaceTexture DepthSurfaceTexture;

	TUniquePtr<avs::Pipeline> Pipeline;
	TArray<avs::Encoder> Encoders;
	TArray<avs::Surface> InputSurfaces;

	FVector2D WorldZToDeviceZTransform;

	FTextureRHIRef				SourceCubemapRHI;
	FUnorderedAccessViewRHIRef UnorderedAccessViewRHIRef;

	FTextureCubeRHIRef LightingTextureCubeRHIRef;
	FUnorderedAccessViewRHIRef LightingUnorderedAccessViewRHIRefs[8];

	ARemotePlayMonitor* Monitor;
};
