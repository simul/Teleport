// (c) 2018 Simul.co

#include "RemotePlayRHI.h"
#include "RemotePlayPlugin.h"

#include "Public/ShaderCore.h"
#include "Public/RenderUtils.h"

#include "AllowWindowsPlatformTypes.h"
#include "D3D11.h"
#include "HideWindowsPlatformTypes.h"

#include "Public/D3D11State.h"
#include "Public/D3D11Resources.h"

#include "dxgiformat.h"

FRemotePlayRHI::FRemotePlayRHI(FRHICommandListImmediate& InRHICmdList, uint32 FrameW, uint32 FrameH)
	: RHICmdList(InRHICmdList)
	, FrameWidth(FrameW)
	, FrameHeight(FrameH)
	, VideoBufferPitch(0)
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
		surface = {};
	}
}

Streaming::Buffer FRemotePlayRHI::createVideoBuffer(Streaming::SurfaceFormat format, int pitch)
{
	uint32 BufferSize = 0;
	uint32 BufferStride = 1;
	uint8 BufferFormat = EPixelFormat::PF_Unknown;

	VideoBufferPitch = (uint32)pitch;

	switch(format)
	{
	case Streaming::SurfaceFormat::ABGR:
	case Streaming::SurfaceFormat::ARGB:
		BufferSize = FrameHeight * pitch * 4;
		BufferStride = 4;
		BufferFormat = EPixelFormat::PF_R8G8B8A8_UINT;
		break;
	case Streaming::SurfaceFormat::NV12:
		BufferSize   = FrameHeight * pitch * 3 / 2;
		BufferStride = 1;
		BufferFormat = EPixelFormat::PF_R8_UINT;
		break;
	default:
		check(0);
	}

	// This is not strictly a "vertex" buffer but this gives us hardware value unpacking which is handy for NV12->RGBA conversion.
	{
		FRHIResourceCreateInfo CreateInfo;
		VideoBufferRHI = RHICmdList.CreateVertexBuffer(BufferSize, BUF_ShaderResource, CreateInfo);
		VideoBufferSRV = RHICmdList.CreateShaderResourceView(VideoBufferRHI, BufferStride, BufferFormat);
	}

	// NOTE: Assuming D3D11 for now.
	FD3D11VertexBuffer* VideoBufferD3D11 = static_cast<FD3D11VertexBuffer*>(VideoBufferRHI.GetReference());
	return Streaming::Buffer{(int)BufferSize, reinterpret_cast<void*>(VideoBufferD3D11->Resource.GetReference())};
}
	
void FRemotePlayRHI::releaseVideoBuffer(Streaming::Buffer& buffer)
{
	if(VideoBufferRHI)
	{
		// NOTE: Assuming D3D11 for now.
		FD3D11VertexBuffer* VideoBufferD3D11 = static_cast<FD3D11VertexBuffer*>(VideoBufferRHI.GetReference());
		if(buffer.pResource == VideoBufferD3D11->Resource.GetReference())
		{
			VideoBufferRHI.SafeRelease();
			buffer = {};
		}
	}
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
