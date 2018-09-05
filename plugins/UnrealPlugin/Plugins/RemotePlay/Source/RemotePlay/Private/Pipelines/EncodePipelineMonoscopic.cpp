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
	}

	FProjectCubemapCS() = default;
	FProjectCubemapCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		InputCubeMap.Bind(Initializer.ParameterMap, TEXT("InputCubeMap"));
		DefaultSampler.Bind(Initializer.ParameterMap, TEXT("DefaultSampler"));
		OutputTexture.Bind(Initializer.ParameterMap, TEXT("OutputTexture"));
	}

	void SetParameters(
		FRHICommandList& RHICmdList,
		FTextureRHIRef InputCubeMapTextureRef,
		FTexture2DRHIRef OutputTextureRef,
		FUnorderedAccessViewRHIRef OutputTextureUAVRef)
	{
		FComputeShaderRHIParamRef ShaderRHI = GetComputeShader();
		SetTextureParameter(RHICmdList, ShaderRHI, InputCubeMap, DefaultSampler, TStaticSamplerState<SF_Bilinear>::GetRHI(), InputCubeMapTextureRef);
		OutputTexture.SetTexture(RHICmdList, ShaderRHI, OutputTextureRef, OutputTextureUAVRef);
	}

	void UnsetParameters(FRHICommandList& RHICmdList)
	{
		FComputeShaderRHIParamRef ShaderRHI = GetComputeShader();
		OutputTexture.UnsetUAV(RHICmdList, ShaderRHI);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << InputCubeMap;
		Ar << DefaultSampler;
		Ar << OutputTexture;
		return bShaderHasOutdatedParameters;
	}

	static const uint32 kThreadGroupSize = 16;

private:
	FShaderResourceParameter InputCubeMap;
	FShaderResourceParameter DefaultSampler;
	FRWShaderParameter OutputTexture;
};

IMPLEMENT_SHADER_TYPE(, FProjectCubemapCS, TEXT("/Plugin/RemotePlay/Private/ProjectCubemap.usf"), TEXT("MainCS"), SF_Compute)

void FEncodePipelineMonoscopic::Initialize(const FRemotePlayEncodeParameters& InParams, avs::Queue& InOutputQueue)
{
	Params = InParams;
	OutputQueue = &InOutputQueue;
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
	OutputQueue = nullptr;
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
	FRemotePlayRHI RHI(RHICmdList);

	FRemotePlayRHI::EDeviceType DeviceType;
	void* DeviceHandle = RHI.GetNativeDevice(DeviceType);
	if(DeviceType == FRemotePlayRHI::EDeviceType::Invalid)
	{
		UE_LOG(LogRemotePlay, Error, TEXT("Failed to obtain native device handle"));
		return;
	}

	InputSurfaceTexture = RHI.CreateSurfaceTexture(Params.FrameWidth, Params.FrameHeight, EPixelFormat::PF_R8G8B8A8);
	if(InputSurfaceTexture.IsValid())
	{
		InputSurfaceUAV = RHI.CreateSurfaceUAV(InputSurfaceTexture);
	}
	else
	{
		UE_LOG(LogRemotePlay, Error, TEXT("Failed to create encoder input surface"));
		return;
	}
	
	avs::DeviceType avsDeviceType = avs::DeviceType::Invalid;
	avs::SurfaceBackendInterface* avsSurfaceBackend = nullptr;

	switch(DeviceType)
	{
#if PLATFORM_WINDOWS
	case FRemotePlayRHI::EDeviceType::DirectX:
		avsDeviceType = avs::DeviceType::DirectX;
		avsSurfaceBackend = new avs::SurfaceDX11(reinterpret_cast<ID3D11Texture2D*>(InputSurfaceTexture->GetNativeResource()));
		break;
#endif
	case FRemotePlayRHI::EDeviceType::OpenGL:
		avsDeviceType = avs::DeviceType::OpenGL;
		// TODO: Create approriate surface backend.
		break;
	default:
		check(0);
	}

	if(!InputSurface.configure(avsSurfaceBackend))
	{
		UE_LOG(LogRemotePlay, Error, TEXT("Failed to configure input surface node"));
		return;
	}

	avs::EncoderParams EncoderParams = {};
	EncoderParams.codec  = avs::VideoCodec::HEVC;
	EncoderParams.preset = avs::VideoPreset::HighPerformance;
	EncoderParams.idrInterval = Params.IDRInterval;
	EncoderParams.targetFrameRate = Params.TargetFPS;
	EncoderParams.averageBitrate = Params.AverageBitrate;
	EncoderParams.maxBitrate = Params.MaxBitrate;
	EncoderParams.deferOutput = Params.bDeferOutput;
	if(!Encoder.configure(avs::DeviceHandle{avsDeviceType, DeviceHandle}, Params.FrameWidth, Params.FrameHeight, EncoderParams))
	{
		UE_LOG(LogRemotePlay, Error, TEXT("Failed to configure encoder"));
		return;
	}

	check(OutputQueue);
	if(!Pipeline.link({&InputSurface, &Encoder, OutputQueue}))
	{
		UE_LOG(LogRemotePlay, Error, TEXT("Error configuring the encoding pipeline"));
		return;
	}
}
	
void FEncodePipelineMonoscopic::Release_RenderThread(FRHICommandListImmediate& RHICmdList)
{
	Encoder.deconfigure();
	InputSurface.deconfigure();
	InputSurfaceUAV.SafeRelease();
	InputSurfaceTexture.SafeRelease();
}
	
void FEncodePipelineMonoscopic::PrepareFrame_RenderThread(
	FRHICommandListImmediate& RHICmdList,
	FTextureRenderTargetResource* TargetResource,
	ERHIFeatureLevel::Type FeatureLevel)
{
	TShaderMap<FGlobalShaderType>* GlobalShaderMap = GetGlobalShaderMap(FeatureLevel);
	TShaderMapRef<FProjectCubemapCS> ComputeShader(GlobalShaderMap);

	const uint32 NumThreadGroupsX = Params.FrameWidth / FProjectCubemapCS::kThreadGroupSize;
	const uint32 NumThreadGroupsY = Params.FrameHeight / FProjectCubemapCS::kThreadGroupSize;
	ComputeShader->SetParameters(RHICmdList, TargetResource->TextureRHI, InputSurfaceTexture, InputSurfaceUAV);
	SetComputePipelineState(RHICmdList, GETSAFERHISHADER_COMPUTE(*ComputeShader));
	DispatchComputeShader(RHICmdList, *ComputeShader, NumThreadGroupsX, NumThreadGroupsY, 1);
	ComputeShader->UnsetParameters(RHICmdList);
}
	
void FEncodePipelineMonoscopic::EncodeFrame_RenderThread(FRHICommandListImmediate& RHICmdList)
{
	if(!Pipeline.process())
	{
		UE_LOG(LogRemotePlay, Warning, TEXT("Encode pipeline processing encountered an error"));
	}
}
