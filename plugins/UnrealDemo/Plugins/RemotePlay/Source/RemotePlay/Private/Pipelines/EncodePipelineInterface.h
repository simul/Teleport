// Copyright 2018 Simul.co

#pragma once

#include "libavstream/libavstream.hpp"

#include "UnrealCasterSettings.h"

namespace SCServer
{
	struct CameraInfo;
	struct CasterContext;
}

class FSceneInterface;
class UTexture;

struct FSurfaceTexture
{
	FTexture2DRHIRef Texture;
	FUnorderedAccessViewRHIRef UAV;
};

class IEncodePipeline
{
public:
	virtual ~IEncodePipeline() = default;

	virtual void Initialize(const FUnrealCasterEncoderSettings& InParams, SCServer::CasterContext* context, class ARemotePlayMonitor* InMonitor, avs::Queue* InColorQueue, avs::Queue* InDepthQueue) = 0;
	virtual void Release() = 0;
	virtual void CullHiddenCubeSegments(FSceneInterface* InScene, SCServer::CameraInfo& CameraInfo, int32 FaceSize, uint32 Divisor) = 0;
	virtual void PrepareFrame(FSceneInterface* InScene, UTexture* InSourceTexture, FTransform& CameraTransform, const TArray<bool>& BlockIntersectionFlags) = 0;
	virtual void EncodeFrame(FSceneInterface* InScene, UTexture* InSourceTexture, FTransform& CameraTransform, bool forceIDR) = 0;
	virtual FSurfaceTexture *GetSurfaceTexture() = 0;
};
