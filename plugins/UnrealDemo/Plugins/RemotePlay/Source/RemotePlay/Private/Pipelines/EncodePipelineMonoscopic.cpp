// Copyright 2018 Simul.co
#define WIN32_LEAN_AND_MEAN

#include "EncodePipelineMonoscopic.h"
#include "RemotePlayModule.h"
#include "RemotePlayRHI.h"

#include "RenderingThread.h"
#include "RHIStaticStates.h"
#include "SceneInterface.h"
#include "SceneUtils.h"

#include "Engine/TextureRenderTargetCube.h"

#include "Public/RHIDefinitions.h"
#include "Public/GlobalShader.h"
#include "Public/PipelineStateCache.h"
#include "Public/ShaderParameters.h"
#include "Public/ShaderParameterUtils.h"
#if PLATFORM_WINDOWS
#include "AllowWindowsPlatformTypes.h"
#include "libavstream/surfaces/surface_dx11.hpp"
#include "libavstream/surfaces/surface_dx12.hpp"
#include "HideWindowsPlatformTypes.h"
#endif
#include <algorithm>

DECLARE_FLOAT_COUNTER_STAT(TEXT("RemotePlayEncodePipelineMonoscopic"), Stat_GPU_RemotePlayEncodePipelineMonoscopic, STATGROUP_GPU);

enum class EProjectCubemapVariant
{
	Color,
	ColorAndDepth,
	ColorAndLinearDepth,
	ColorAndLinearDepthToTexture,
	ColorAndLinearDepthStacked
};

template<EProjectCubemapVariant Variant>
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
		OutEnvironment.SetDefine(TEXT("WRITE_DEPTH_LINEAR"), bWriteLinearDepth ? 1 : 0);
		OutEnvironment.SetDefine(TEXT("DEPTH_STACKED"), bWriteStacked ? 1 : 0);
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
		if(bWriteLinearDepth)
		{
			WorldZToDeviceZTransform.Bind(Initializer.ParameterMap, TEXT("WorldZToDeviceZTransform"));
		}
		if (bWriteStacked)
		{
			DepthPos.Bind(Initializer.ParameterMap, TEXT("DepthPos"));
		}
	}
	 
	void SetParameters(
		FRHICommandList& RHICmdList,
		FTextureRHIRef InputCubeMapTextureRef,
		FTexture2DRHIRef OutputColorTextureRef,
		FUnorderedAccessViewRHIRef OutputColorTextureUAVRef,
		FTexture2DRHIRef OutputDepthTextureRef,
		FUnorderedAccessViewRHIRef OutputDepthTextureUAVRef,
		const FVector2D& InWorldZToDeviceZTransform,
		uint32_t InDepthPos)
	{
		FComputeShaderRHIParamRef ShaderRHI = GetComputeShader();

		SetTextureParameter(RHICmdList, ShaderRHI, InputCubeMap, DefaultSampler, TStaticSamplerState<SF_Bilinear>::GetRHI(), InputCubeMapTextureRef);
		OutputColorTexture.SetTexture(RHICmdList, ShaderRHI, OutputColorTextureRef, OutputColorTextureUAVRef);

		if(bWriteDepthTexture)
		{
			OutputDepthTexture.SetTexture(RHICmdList, ShaderRHI, OutputDepthTextureRef, OutputDepthTextureUAVRef);
		}
		if(bWriteLinearDepth)
		{
			SetShaderValue(RHICmdList, ShaderRHI, WorldZToDeviceZTransform, InWorldZToDeviceZTransform);
		}
		if (bWriteStacked)
		{
			SetShaderValue(RHICmdList, ShaderRHI, DepthPos, InDepthPos);
		}
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
		if(bWriteDepthTexture)
		{
			Ar << OutputDepthTexture;
		}
		if(bWriteLinearDepth)
		{
			Ar << WorldZToDeviceZTransform;
		}
		if (bWriteStacked)
		{
			Ar << DepthPos;
		}
		return bShaderHasOutdatedParameters;
	}

	static const uint32 kThreadGroupSize = 16;
	static const bool bWriteDepth = (Variant != EProjectCubemapVariant::Color);
	static const bool bWriteLinearDepth = (Variant == EProjectCubemapVariant::ColorAndLinearDepth || Variant == EProjectCubemapVariant::ColorAndLinearDepthToTexture || Variant == EProjectCubemapVariant::ColorAndLinearDepthStacked);
	static const bool bWriteDepthTexture = (Variant == EProjectCubemapVariant::ColorAndLinearDepthToTexture);
	static const bool bWriteStacked = (Variant == EProjectCubemapVariant::ColorAndLinearDepthStacked);

private:
	FShaderResourceParameter InputCubeMap;
	FShaderResourceParameter DefaultSampler;
	FRWShaderParameter OutputColorTexture;
	FRWShaderParameter OutputDepthTexture;
	FShaderParameter WorldZToDeviceZTransform;
	FShaderParameter DepthPos;
};

IMPLEMENT_SHADER_TYPE(, FProjectCubemapCS<EProjectCubemapVariant::Color>, TEXT("/Plugin/RemotePlay/Private/ProjectCubemap.usf"), TEXT("MainCS"), SF_Compute)
IMPLEMENT_SHADER_TYPE(, FProjectCubemapCS<EProjectCubemapVariant::ColorAndDepth>,  TEXT("/Plugin/RemotePlay/Private/ProjectCubemap.usf"), TEXT("MainCS"), SF_Compute)
IMPLEMENT_SHADER_TYPE(, FProjectCubemapCS<EProjectCubemapVariant::ColorAndLinearDepth>,  TEXT("/Plugin/RemotePlay/Private/ProjectCubemap.usf"), TEXT("MainCS"), SF_Compute)
IMPLEMENT_SHADER_TYPE(, FProjectCubemapCS<EProjectCubemapVariant::ColorAndLinearDepthStacked>, TEXT("/Plugin/RemotePlay/Private/ProjectCubemap.usf"), TEXT("MainCS"), SF_Compute)
IMPLEMENT_SHADER_TYPE(, FProjectCubemapCS<EProjectCubemapVariant::ColorAndLinearDepthToTexture>, TEXT("/Plugin/RemotePlay/Private/ProjectCubemap.usf"), TEXT("MainCS"), SF_Compute)

static inline FVector2D CreateWorldZToDeviceZTransform(float FOV)
{
	FMatrix ProjectionMatrix;
	if(static_cast<int32>(ERHIZBuffer::IsInverted) == 1)
	{
		ProjectionMatrix = FReversedZPerspectiveMatrix(FOV, FOV, 1.0f, 1.0f, GNearClippingPlane, GNearClippingPlane);
	}
	else
	{
		ProjectionMatrix = FPerspectiveMatrix(FOV, FOV, 1.0f, 1.0f, GNearClippingPlane, GNearClippingPlane);
	}

	// Based on CreateInvDeviceZToWorldZTransform() in Runtime\Engine\Private\SceneView.cpp.
	float DepthMul = ProjectionMatrix.M[2][2];
	float DepthAdd = ProjectionMatrix.M[3][2];

	if(DepthAdd == 0.0f)
	{
		DepthAdd = 0.00000001f;
	}

	float SubtractValue = DepthMul / DepthAdd;
	SubtractValue -= 0.00000001f;

	return FVector2D{1.0f / DepthAdd, SubtractValue};
}

void FEncodePipelineMonoscopic::Initialize(const FRemotePlayEncodeParameters& InParams, avs::Queue* InColorQueue, avs::Queue* InDepthQueue)
{
	check(InColorQueue);

	Params = InParams;
	ColorQueue = InColorQueue;
	DepthQueue = InDepthQueue;
	WorldZToDeviceZTransform = CreateWorldZToDeviceZTransform(FMath::DegreesToRadians(90.0f));

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

	EPixelFormat PixelFormat = EPixelFormat::PF_R8G8B8A8;

	switch(DeviceType)
	{
	case FRemotePlayRHI::EDeviceType::Direct3D11:
		avsDeviceType = avs::DeviceType::Direct3D11;
		break;
	case FRemotePlayRHI::EDeviceType::Direct3D12:
		avsDeviceType = avs::DeviceType::Direct3D12;
		PixelFormat = EPixelFormat::PF_B8G8R8A8;
		break;
	case FRemotePlayRHI::EDeviceType::OpenGL:
		avsDeviceType = avs::DeviceType::OpenGL;
		break;
	default:
		UE_LOG(LogRemotePlay, Error, TEXT("Failed to obtain native device handle"));
		return; 
	} 
	// Roderick: we create a DOUBLE-HEIGHT texture, and encode colour in the top half, depth in the bottom.
	int32 w = std::max<int32>(Params.FrameWidth, Params.DepthWidth);
	
	ColorSurfaceTexture.Texture = RHI.CreateSurfaceTexture(w, Params.FrameHeight+Params.DepthHeight, PixelFormat);
	
	if(ColorSurfaceTexture.Texture.IsValid())
	{
		ColorSurfaceTexture.UAV = RHI.CreateSurfaceUAV(ColorSurfaceTexture.Texture);
#if PLATFORM_WINDOWS
		if (avsDeviceType == avs::DeviceType::Direct3D11)
		{
			avsSurfaceBackends[0] = new avs::SurfaceDX11(reinterpret_cast<ID3D11Texture2D*>(ColorSurfaceTexture.Texture->GetNativeResource()));
		}
		if(avsDeviceType == avs::DeviceType::Direct3D12) 
		{
			avsSurfaceBackends[0] = new avs::SurfaceDX12(reinterpret_cast<ID3D12Resource*>(ColorSurfaceTexture.Texture->GetNativeResource()));
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
			if(avsDeviceType == avs::DeviceType::Direct3D11)
			{
				avsSurfaceBackends[1] = new avs::SurfaceDX11(reinterpret_cast<ID3D11Texture2D*>(DepthSurfaceTexture.Texture->GetNativeResource()));
			}
			if (avsDeviceType == avs::DeviceType::Direct3D12)
			{
				avsSurfaceBackends[1] = new avs::SurfaceDX12(reinterpret_cast<ID3D12Resource*>(DepthSurfaceTexture.Texture->GetNativeResource()));
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
	if(!Params.bLinearDepth)
	{
		EncoderParams.depthRemapNear = GNearClippingPlane;
		EncoderParams.depthRemapFar = Params.MaxDepth;
	}

	Pipeline.Reset(new avs::Pipeline);
	Encoders.SetNum(NumStreams);
	InputSurfaces.SetNum(NumStreams);

	for(uint32_t i=0; i<NumStreams; ++i)
	{ 
		if(!InputSurfaces[i].configure(avsSurfaceBackends[i]))
		{
			UE_LOG(LogRemotePlay, Error, TEXT("Failed to configure input surface node #%d"), i);
			return;
		}
		EncoderParams.inputFormat = avsInputFormats[i];
		if(!Encoders[i].configure(avs::DeviceHandle{avsDeviceType, DeviceHandle}, Params.FrameWidth, Params.FrameHeight+Params.DepthHeight, EncoderParams))
		{
			UE_LOG(LogRemotePlay, Error, TEXT("Failed to configure encoder #%d"), i);
			return;
		}

		if(!Pipeline->link({&InputSurfaces[i], &Encoders[i], Outputs[i]}))
		{
			UE_LOG(LogRemotePlay, Error, TEXT("Error configuring the encoding pipeline"));
			return;
		}
	}
}
	
void FEncodePipelineMonoscopic::Release_RenderThread(FRHICommandListImmediate& RHICmdList)
{
	Pipeline.Reset();
	Encoders.Empty();
	InputSurfaces.Empty();

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
	if(DepthQueue)
	{
		if (Params.bLinearDepth)
		{
			DispatchProjectCubemapShader<FProjectCubemapCS<EProjectCubemapVariant::ColorAndLinearDepthToTexture>>(RHICmdList, TargetResource->TextureRHI, FeatureLevel);
		}
		else
		{
			DispatchProjectCubemapShader<FProjectCubemapCS<EProjectCubemapVariant::ColorAndLinearDepthToTexture>>(RHICmdList, TargetResource->TextureRHI, FeatureLevel);
		}
	}
	else
	{
		if (Params.bStackDepth)
		{
			DispatchProjectCubemapShader<FProjectCubemapCS<EProjectCubemapVariant::ColorAndLinearDepthStacked>>(RHICmdList, TargetResource->TextureRHI, FeatureLevel);
		}
		else if (Params.bLinearDepth)
		{
			DispatchProjectCubemapShader<FProjectCubemapCS<EProjectCubemapVariant::ColorAndLinearDepth>>(RHICmdList, TargetResource->TextureRHI, FeatureLevel);
		}
		else
		{
			DispatchProjectCubemapShader<FProjectCubemapCS<EProjectCubemapVariant::ColorAndDepth>>(RHICmdList, TargetResource->TextureRHI, FeatureLevel);
		}
		//DispatchProjectCubemapShader<FProjectCubemapCS<EProjectCubemapVariant::Color>>(RHICmdList, TargetResource->TextureRHI, FeatureLevel);
	}
}
	
void FEncodePipelineMonoscopic::EncodeFrame_RenderThread(FRHICommandListImmediate& RHICmdList)
{
	check(Pipeline.IsValid());
	// The transform of the capture component needs to be sent with the image
	FTransform Transform;
	if (CameraTransformQueue.Dequeue(Transform))
	{
		avs::Transform CamTransform;
		const FVector t = Transform.GetTranslation();
		const FQuat r = Transform.GetRotation();
		const FVector s = Transform.GetScale3D();
		CamTransform = { t.X, t.Y, t.Z, r.X, r.Y, r.Z, r.W, s.X, s.Y, s.Z };
		for (auto& Encoder : Encoders)
		{
			Encoder.setCameraTransform(CamTransform);
		}
	}
	if (!Pipeline->process())
	{
		UE_LOG(LogRemotePlay, Warning, TEXT("Encode pipeline processing encountered an error"));
	}
}

template<typename ShaderType>
void FEncodePipelineMonoscopic::DispatchProjectCubemapShader(FRHICommandListImmediate& RHICmdList, FTextureRHIRef TextureRHI, ERHIFeatureLevel::Type FeatureLevel)
{
	const uint32 NumThreadGroupsX = Params.FrameWidth / ShaderType::kThreadGroupSize;
	const uint32 NumThreadGroupsY = Params.FrameHeight / ShaderType::kThreadGroupSize;

	TShaderMap<FGlobalShaderType>* GlobalShaderMap = GetGlobalShaderMap(FeatureLevel);
			 
	TShaderMapRef<ShaderType> ComputeShader(GlobalShaderMap);
	ComputeShader->SetParameters(RHICmdList, TextureRHI,
		ColorSurfaceTexture.Texture, ColorSurfaceTexture.UAV, 
		DepthSurfaceTexture.Texture, DepthSurfaceTexture.UAV,
		WorldZToDeviceZTransform, Params.FrameHeight);
	SetComputePipelineState(RHICmdList, GETSAFERHISHADER_COMPUTE(*ComputeShader));
	DispatchComputeShader(RHICmdList, *ComputeShader, NumThreadGroupsX, NumThreadGroupsY, 1);
	ComputeShader->UnsetParameters(RHICmdList);
}

void FEncodePipelineMonoscopic::AddCameraTransform(FTransform& Transform)
{
	CameraTransformQueue.Enqueue(Transform);
}
  
