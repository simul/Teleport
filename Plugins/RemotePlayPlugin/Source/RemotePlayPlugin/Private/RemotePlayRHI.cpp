// (c) 2018 Simul.co

#include "RemotePlayRHI.h"
#include "RemotePlayPlugin.h"

#include "Public/RenderUtils.h"

#include "dxgiformat.h"

FRemotePlayRHI::FRemotePlayRHI(FRHICommandListImmediate& InRHICmdList, uint32 FrameW, uint32 FrameH)
	: RHICmdList(InRHICmdList)
	, FrameWidth(FrameW)
	, FrameHeight(FrameH)
{}

Streaming::Surface FRemotePlayRHI::createSurface(Streaming::SurfaceFormat format)
{
	FRHIResourceCreateInfo CreateInfo;
	EPixelFormat PixelFormat = EPixelFormat::PF_Unknown;
	switch(format)
	{
	case Streaming::SurfaceFormat::ARGB:
		PixelFormat = EPixelFormat::PF_R8G8B8A8;
		break;
	case Streaming::SurfaceFormat::ABGR:
		PixelFormat = EPixelFormat::PF_B8G8R8A8;
		break;
	case Streaming::SurfaceFormat::NV12:
		check(0);
		break;
	}

	// HACK: NVENC requires a typed D3D11 texture, we're forcing RGBA_UNORM!
	if(PixelFormat == EPixelFormat::PF_R8G8B8A8)
	{
		const uint32 OldFormat = GPixelFormats[EPixelFormat::PF_R8G8B8A8].PlatformFormat;
		GPixelFormats[EPixelFormat::PF_R8G8B8A8].PlatformFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
		SurfaceRHI = RHICmdList.CreateTexture2D(FrameWidth, FrameHeight, PixelFormat, 1, 1, TexCreate_UAV | TexCreate_RenderTargetable, CreateInfo);
		GPixelFormats[EPixelFormat::PF_R8G8B8A8].PlatformFormat = OldFormat;
	}
	else
	{
		SurfaceRHI = RHICmdList.CreateTexture2D(FrameWidth, FrameHeight, PixelFormat, 1, 1, TexCreate_UAV | TexCreate_RenderTargetable, CreateInfo);
	}

	SurfaceUAV = RHICmdList.CreateUnorderedAccessView(SurfaceRHI, 0);

	return Streaming::Surface{(int)FrameWidth, (int)FrameHeight, SurfaceRHI->GetNativeResource()};
}
	
void FRemotePlayRHI::releaseSurface(Streaming::Surface& surface)
{
	if(SurfaceRHI && surface.pResource == SurfaceRHI->GetNativeResource())
	{
		SurfaceUAV.SafeRelease();
		SurfaceRHI.SafeRelease();
	}
}

Streaming::Buffer FRemotePlayRHI::createVideoBuffer(Streaming::SurfaceFormat format, int pitch)
{
	checkf(0, TEXT("Not implemented!"));
	return Streaming::Buffer{};
}
	
void FRemotePlayRHI::releaseVideoBuffer(Streaming::Buffer& buffer)
{
	checkf(0, TEXT("Not implemented!"));
}

Streaming::RendererDevice* FRemotePlayRHI::getDevice() const
{
	const EShaderPlatform ShaderPlatform = GMaxRHIShaderPlatform;
	if(ShaderPlatform == SP_PCD3D_SM5)
	{
		check(GDynamicRHI);
		return reinterpret_cast<Streaming::RendererDevice*>(GDynamicRHI->RHIGetNativeDevice());
	}
	else
	{
		UE_LOG(LogRemotePlayPlugin, Error, TEXT("Unsupported RHI shader platform!"));
		return nullptr;
	}
}
