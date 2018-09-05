// Copyright 2018 Simul.co

#pragma once

#include "RemotePlayParameters.h"
#include "libavstream/libavstream.hpp"

class FSceneInterface;
class UTexture;

class IEncodePipeline
{
public:
	virtual ~IEncodePipeline() = default;

	virtual void Initialize(const FRemotePlayEncodeParameters& InParams, avs::Queue& InOutputQueue) = 0;
	virtual void Release() = 0;
	virtual void EncodeFrame(FSceneInterface* InScene, UTexture* InSourceTexture) = 0;
};
