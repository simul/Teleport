// Copyright 2018 Simul.co

#pragma once

#include "RemotePlayParameters.h"
#include "libavstream/libavstream.hpp"

class FSceneInterface;
class UTexture;

struct FSurfaceTexture
{
	FTexture2DRHIRef Texture;
	FUnorderedAccessViewRHIRef UAV;
};

struct FCameraInfo
{
	FQuat Orientation = { 0, 0, 0, 1 };
	FVector Position = FVector::ZeroVector;
	float FOV = 90;
	float Width = 1920;
	float Height = 1080;
	bool isVR = false;
};

class IEncodePipeline
{
public:
	virtual ~IEncodePipeline() = default;

	virtual void Initialize(const FRemotePlayEncodeParameters& InParams, struct FRemotePlayContext *context, class ARemotePlayMonitor* InMonitor, avs::Queue* InColorQueue, avs::Queue* InDepthQueue) = 0;
	virtual void Release() = 0;
	virtual void CullHiddenCubeSegments(FSceneInterface* InScene, FCameraInfo& CameraInfo, int32 FaceSize, uint32 Divisor) = 0;
	virtual void PrepareFrame(FSceneInterface* InScene, UTexture* InSourceTexture, FTransform& CameraTransform, const TArray<bool>& BlockIntersectionFlags) = 0;
	virtual void EncodeFrame(FSceneInterface* InScene, UTexture* InSourceTexture, FTransform& CameraTransform, bool forceIDR) = 0;
	virtual FSurfaceTexture *GetSurfaceTexture() = 0;
};
