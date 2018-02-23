// (c) 2018 Simul.co

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "LibStreaming.hpp"

class FRemotePlayRHI : public Streaming::RendererInterface
{
public:
	FRemotePlayRHI(FRHICommandListImmediate& InRHICmdList, uint32 FrameW, uint32 FrameH);

	/* Begin Streaming::RendererInterface */
	virtual Streaming::Surface createSurface(Streaming::SurfaceFormat format) override;
	virtual void releaseSurface(Streaming::Surface& surface) override;

	virtual Streaming::Buffer createVideoBuffer(Streaming::SurfaceFormat format, int pitch) override;
	virtual void releaseVideoBuffer(Streaming::Buffer& buffer) override;

	virtual Streaming::RendererDevice* getDevice() const override;
	/* End Streaming::RendererInterface */

	FTexture2DRHIRef SurfaceRHI;
	FUnorderedAccessViewRHIRef SurfaceUAV;

	FVertexBufferRHIRef VideoBufferRHI;
	FShaderResourceViewRHIRef VideoBufferSRV;
	uint32 VideoBufferPitch;

	const uint32 FrameWidth;
	const uint32 FrameHeight;
private:
	FRHICommandListImmediate& RHICmdList;
};
