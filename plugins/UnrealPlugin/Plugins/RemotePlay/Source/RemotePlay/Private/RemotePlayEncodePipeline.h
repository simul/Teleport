// Copyright 2018 Simul.co

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"

#include "RemotePlayParameters.h"

#include "libavstream/libavstream.hpp"

class UTextureRenderTargetCube;
class FTextureRenderTargetResource;

class FRemotePlayEncodePipeline
{
public:
	FRemotePlayEncodePipeline(const FRemotePlayEncodeParameters& InParams, avs::Queue& InOutputQueue);

	void Initialize();
	void Release();
	void EncodeFrame(FSceneInterface* InScene, UTextureRenderTargetCube* InTarget);

private:
	void Initialize_RenderThread(FRHICommandListImmediate& RHICmdList);
	void Release_RenderThread(FRHICommandListImmediate& RHICmdList);
	void PrepareFrame_RenderThread(FRHICommandListImmediate& RHICmdList, FTextureRenderTargetResource* TargetResource, ERHIFeatureLevel::Type FeatureLevel);
	void EncodeFrame_RenderThread(FRHICommandListImmediate& RHICmdList);

	const FRemotePlayEncodeParameters Params;
	FTexture2DRHIRef InputSurfaceTexture;
	FUnorderedAccessViewRHIRef InputSurfaceUAV;

	avs::Pipeline Pipeline;
	avs::Encoder Encoder;
	avs::Surface InputSurface;
	avs::Queue&  OutputQueue;
};
