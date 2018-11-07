// Copyright 2018 Simul.co

#include "EncodePipelineMonoscopic.h"
#include "RemotePlayModule.h"
#include "RemotePlayRHI.h"

#include "RenderingThread.h"
#include "RHIStaticStates.h"
#include "SceneInterface.h"
#include "SceneUtils.h"

#include "Engine/TextureRenderTargetCube.h"

#include "Public/GlobalShader.h"
#include "Public/PipelineStateCache.h"
#include "Public/ShaderParameters.h"
#include "Public/ShaderParameterUtils.h"

#if PLATFORM_WINDOWS
#include "AllowWindowsPlatformTypes.h"
#include "libavstream/surfaces/surface_dx11.hpp"
#include "HideWindowsPlatformTypes.h"
#endif

DECLARE_FLOAT_COUNTER_STAT(TEXT("RemotePlayEncodePipelineMonoscopic"), Stat_GPU_RemotePlayEncodePipelineMonoscopic, STATGROUP_GPU);

template<bool bWriteDepth>
class FProjectCubemapCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FProjectCubemapCS, Global);
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return Parameters.Platform == EShaderPlatform::SP_PCD3D_SM5;
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), kThreadGroupSize);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEY"), kThreadGroupSize);
		OutEnvironment.SetDefine(TEXT("WRITE_DEPTH"), bWriteDepth ? 1 : 0);
	}

	FProjectCubemapCS() = default;
	FProjectCubemapCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		InputCubeMap.Bind(Initializer.ParameterMap, TEXT("InputCubeMap"));
		DefaultSampler.Bind(Initializer.ParameterMap, TEXT("DefaultSampler"));
		OutputColorTexture.Bind(Initializer.ParameterMap, TEXT("OutputColorTexture"));
		if(bWriteDepth)
		{
			OutputDepthTexture.Bind(Initializer.ParameterMap, TEXT("OutputDepthTexture"));
		}
	}

	void SetParameters(
		FRHICommandList& RHICmdList,
		FTextureRHIRef InputCubeMapTextureRef,
		FTexture2DRHIRef OutputColorTextureRef,
		FUnorderedAccessViewRHIRef OutputColorTextureUAVRef)
	{
		FComputeShaderRHIParamRef ShaderRHI = GetComputeShader();
		SetTextureParameter(RHICmdList, ShaderRHI, InputCubeMap, DefaultSampler, TStaticSamplerState<SF_Bilinear>::GetRHI(), InputCubeMapTextureRef);
		OutputColorTexture.SetTexture(RHICmdList, ShaderRHI, OutputColorTextureRef, OutputColorTextureUAVRef);
	}
	
	void SetParameters(
		FRHICommandList& RHICmdList,
		FTextureRHIRef InputCubeMapTextureRef,
		FTexture2DRHIRef OutputColorTextureRef,
		FUnorderedAccessViewRHIRef OutputColorTextureUAVRef,
		FTexture2DRHIRef OutputDepthTextureRef,
		FUnorderedAccessViewRHIRef OutputDepthTextureUAVRef)
	{
		check(bWriteDepth);
		FComputeShaderRHIParamRef ShaderRHI = GetComputeShader();
		SetParameters(RHICmdList, InputCubeMapTextureRef, OutputColorTextureRef, OutputColorTextureUAVRef);
		OutputDepthTexture.SetTexture(RHICmdList, ShaderRHI, OutputDepthTextureRef, OutputDepthTextureUAVRef);
	}

	void UnsetParameters(FRHICommandList& RHICmdList)
	{
		FComputeShaderRHIParamRef ShaderRHI = GetComputeShader();
		OutputColorTexture.UnsetUAV(RHICmdList, ShaderRHI);
		if(bWriteDepth)
		{
			OutputDepthTexture.UnsetUAV(RHICmdList, ShaderRHI);
		}
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << InputCubeMap;
		Ar << DefaultSampler;
		Ar << OutputColorTexture;
		if(bWriteDepth)
		{
			Ar << OutputDepthTexture;
		}
		return bShaderHasOutdatedParameters;
	}

	static const uint32 kThreadGroupSize = 16;

private:
	FShaderResourceParameter InputCubeMap;
	FShaderResourceParameter DefaultSampler;
	FRWShaderParameter OutputColorTexture;
	FRWShaderParameter OutputDepthTexture;
};

IMPLEMENT_SHADER_TYPE(, FProjectCubemapCS<false>, TEXT("/Plugin/RemotePlay/Private/ProjectCubemap.usf"), TEXT("MainCS"), SF_Compute)
IMPLEMENT_SHADER_TYPE(, FProjectCubemapCS<true>,  TEXT("/Plugin/RemotePlay/Private/ProjectCubemap.usf"), TEXT("MainCS"), SF_Compute)

void FEncodePipelineMonoscopic::Initialize(const FRemotePlayEncodeParameters& InParams, avs::Queue* InColorQueue, avs::Queue* InDepthQueue)
{
	check(InColorQueue);

	Params = InParams;
	ColorQueue = InColorQueue;
	DepthQueue = InDepthQueue;
	ENQUEUE_RENDER_COMMAND(RemotePlayInitializeEncodePipeline)(
		[this](FRHICommandListImmediate& RHICmdList)
		{
			Initialize_RenderThread(RHICmdList);
		}
	);
}

void FEncodePipelineMonoscopic::Release()
{
	ENQUEUE_RENDER_COMMAND(RemotePlayReleaseEncodePipeline)(
		[this](FRHICommandListImmediate& RHICmdList)
		{
			Release_RenderThread(RHICmdList);
		}
	);
	FlushRenderingCommands();
	ColorQueue = nullptr;
	DepthQueue = nullptr;
}

void FEncodePipelineMonoscopic::EncodeFrame(FSceneInterface* InScene, UTexture* InSourceTexture)
{
	if(!InScene || !InSourceTexture)
	{
		return;
	}

	const ERHIFeatureLevel::Type FeatureLevel = InScene->GetFeatureLevel();

	auto SourceTarget = CastChecked<UTextureRenderTargetCube>(InSourceTexture);
	FTextureRenderTargetResource* TargetResource = SourceTarget->GameThread_GetRenderTargetResource();

	ENQUEUE_RENDER_COMMAND(RemotePlayEncodeFrame)(
		[this, TargetResource, FeatureLevel](FRHICommandListImmediate& RHICmdList)
		{
			SCOPED_DRAW_EVENT(RHICmdList, RemotePlayEncodePipelineMonoscopic);
			PrepareFrame_RenderThread(RHICmdList, TargetResource, FeatureLevel);
			EncodeFrame_RenderThread(RHICmdList);
		}
	);
}
	
void FEncodePipelineMonoscopic::Initialize_RenderThread(FRHICommandListImmediate& RHICmdList)
{
	const uint32_t NumStreams = (DepthQueue != nullptr) ? 2 : 1;
	avs::Queue* const Outputs[] = { ColorQueue, DepthQueue };

	FRemotePlayRHI RHI(RHICmdList);
	FRemotePlayRHI::EDeviceType DeviceType;
	void* DeviceHandle = RHI.GetNativeDevice(DeviceType);

	avs::DeviceType avsDeviceType;
	avs::SurfaceBackendInterface* avsSurfaceBackends[2] = { nullptr };

	const avs::SurfaceFormat avsInputFormats[2] = {
		avs::SurfaceFormat::Unknown, // Any suitable for color (preferably ARGB or ABGR)
		avs::SurfaceFormat::NV12, // NV12 is needed for depth encoding
	};

	switch(DeviceType)
	{
	case FRemotePlayRHI::EDeviceType::DirectX:
		avsDeviceType = avs::DeviceType::DirectX;
		break;
	case FRemotePlayRHI::EDeviceType::OpenGL:
		avsDeviceType = avs::DeviceType::OpenGL;
		break;
	default:
		UE_LOG(LogRemotePlay, Error, TEXT("Failed to obtain native device handle"));
		return;
	}

	ColorSurfaceTexture.Texture = RHI.CreateSurfaceTexture(Params.FrameWidth, Params.FrameHeight, EPixelFormat::PF_R8G8B8A8);
	if(ColorSurfaceTexture.Texture.IsValid())
	{
		ColorSurfaceTexture.UAV = RHI.CreateSurfaceUAV(ColorSurfaceTexture.Texture);
#if PLATFORM_WINDOWS
		if(avsDeviceType == avs::DeviceType::DirectX)
		{
			avsSurfaceBackends[0] = new avs::SurfaceDX11(reinterpret_cast<ID3D11Texture2D*>(ColorSurfaceTexture.Texture->GetNativeResource()));
		}
#endif
		if(avsDeviceType == avs::DeviceType::OpenGL)
		{
			// TODO: Implement
		}
	}
	else
	{
		UE_LOG(LogRemotePlay, Error, TEXT("Failed to create encoder color input surface texture"));
		return;
	}

	if(DepthQueue)
	{
		DepthSurfaceTexture.Texture = RHI.CreateSurfaceTexture(Params.FrameWidth, Params.FrameHeight, EPixelFormat::PF_R16F);
		if(DepthSurfaceTexture.Texture.IsValid())
		{
			DepthSurfaceTexture.UAV = RHI.CreateSurfaceUAV(DepthSurfaceTexture.Texture);
#if PLATFORM_WINDOWS
			if(avsDeviceType == avs::DeviceType::DirectX)
			{
				avsSurfaceBackends[1] = new avs::SurfaceDX11(reinterpret_cast<ID3D11Texture2D*>(DepthSurfaceTexture.Texture->GetNativeResource()));
			}
#endif
			if(avsDeviceType == avs::DeviceType::OpenGL)
			{
				// TODO: Implement
			}
		}
		else
		{
			ColorSurfaceTexture.Texture.SafeRelease();
			ColorSurfaceTexture.UAV.SafeRelease();
			UE_LOG(LogRemotePlay, Error, TEXT("Failed to create encoder depth input surface texture"));
			return;
		}
	}
	
	avs::EncoderParams EncoderParams = {};
	EncoderParams.codec  = avs::VideoCodec::HEVC;
	EncoderParams.preset = avs::VideoPreset::HighPerformance;
	EncoderParams.idrInterval = Params.IDRInterval;
	EncoderParams.targetFrameRate = Params.TargetFPS;
	EncoderParams.averageBitrate = Params.AverageBitrate;
	EncoderParams.maxBitrate = Params.MaxBitrate;
	EncoderParams.deferOutput = Params.bDeferOutput;

	Pipeline.Reset(new avs::Pipeline);
	Encoder.SetNum(NumStreams);
	InputSurface.SetNum(NumStreams);
	for(uint32_t i=0; i<NumStreams; ++i)
	{
		if(!InputSurface[i].configure(avsSurfaceBackends[i]))
		{
			UE_LOG(LogRemotePlay, Error, TEXT("Failed to configure input surface node #%d"), i);
			return;
		}

		EncoderParams.inputFormat = avsInputFormats[i];
		if(!Encoder[i].configure(avs::DeviceHandle{avsDeviceType, DeviceHandle}, Params.FrameWidth, Params.FrameHeight, EncoderParams))
		{
			UE_LOG(LogRemotePlay, Error, TEXT("Failed to configure encoder #%d"), i);
			return;
		}

		if(!Pipeline->link({&InputSurface[i], &Encoder[i], Outputs[i]}))
		{
			UE_LOG(LogRemotePlay, Error, TEXT("Error configuring the encoding pipeline"));
			return;
		}
	}
}
	
void FEncodePipelineMonoscopic::Release_RenderThread(FRHICommandListImmediate& RHICmdList)
{
	Pipeline.Reset();
	Encoder.Empty();
	InputSurface.Empty();

	ColorSurfaceTexture.Texture.SafeRelease();
	ColorSurfaceTexture.UAV.SafeRelease();

	DepthSurfaceTexture.Texture.SafeRelease();
	DepthSurfaceTexture.UAV.SafeRelease();
}
	
void FEncodePipelineMonoscopic::PrepareFrame_RenderThread(
	FRHICommandListImmediate& RHICmdList,
	FTextureRenderTargetResource* TargetResource,
	ERHIFeatureLevel::Type FeatureLevel)
{
	const uint32 NumThreadGroupsX = Params.FrameWidth / FProjectCubemapCS<false>::kThreadGroupSize;
	const uint32 NumThreadGroupsY = Params.FrameHeight / FProjectCubemapCS<false>::kThreadGroupSize;

	TShaderMap<FGlobalShaderType>* GlobalShaderMap = GetGlobalShaderMap(FeatureLevel);

	if(DepthQueue)
	{
		TShaderMapRef<FProjectCubemapCS<true>> ComputeShader(GlobalShaderMap);
		ComputeShader->SetParameters(RHICmdList, TargetResource->TextureRHI,
			ColorSurfaceTexture.Texture, ColorSurfaceTexture.UAV,
			DepthSurfaceTexture.Texture, DepthSurfaceTexture.UAV);
		SetComputePipelineState(RHICmdList, GETSAFERHISHADER_COMPUTE(*ComputeShader));
		DispatchComputeShader(RHICmdList, *ComputeShader, NumThreadGroupsX, NumThreadGroupsY, 1);
		ComputeShader->UnsetParameters(RHICmdList);
	}
	else
	{
		TShaderMapRef<FProjectCubemapCS<false>> ComputeShader(GlobalShaderMap);
		ComputeShader->SetParameters(RHICmdList, TargetResource->TextureRHI,
			ColorSurfaceTexture.Texture, ColorSurfaceTexture.UAV);
		SetComputePipelineState(RHICmdList, GETSAFERHISHADER_COMPUTE(*ComputeShader));
		DispatchComputeShader(RHICmdList, *ComputeShader, NumThreadGroupsX, NumThreadGroupsY, 1);
		ComputeShader->UnsetParameters(RHICmdList);
	}
}
	
void FEncodePipelineMonoscopic::EncodeFrame_RenderThread(FRHICommandListImmediate& RHICmdList)
{
	check(Pipeline.IsValid());
	if(!Pipeline->process())
	{
		UE_LOG(LogRemotePlay, Warning, TEXT("Encode pipeline processing encountered an error"));
	}
}
