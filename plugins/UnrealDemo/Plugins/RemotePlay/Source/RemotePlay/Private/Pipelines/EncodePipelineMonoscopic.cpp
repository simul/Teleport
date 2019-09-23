// Copyright 2018 Simul.co
#define WIN32_LEAN_AND_MEAN

#include "EncodePipelineMonoscopic.h"
#include "RemotePlayModule.h"
#include "RemotePlayRHI.h"

#include "RenderingThread.h"
#include "RHIStaticStates.h"
#include "SceneInterface.h"
#include "SceneUtils.h"
#include "RemotePlayContext.h"

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
	ColorAndLinearDepthStacked,
	DecomposeCubemaps,
	DecomposeDepth
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
		RWInputCubeAsArray.Bind(Initializer.ParameterMap, TEXT("InputCubeAsArray"));
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
		if (bDecomposeCubemaps)
		{
			Offset.Bind(Initializer.ParameterMap, TEXT("Offset"));
		}
	}
	 
	void SetParameters(
		FRHICommandList& RHICmdList,
		FTextureRHIRef InputCubeMapTextureRef,
		FUnorderedAccessViewRHIRef InputCubeMapTextureUAVRef,
		FTexture2DRHIRef OutputColorTextureRef,
		FUnorderedAccessViewRHIRef OutputColorTextureUAVRef,
		FTexture2DRHIRef OutputDepthTextureRef,
		FUnorderedAccessViewRHIRef OutputDepthTextureUAVRef,
		const FVector2D& InWorldZToDeviceZTransform,
		const FIntPoint& InOffset,
		uint32_t InDepthPos)
	{
		FComputeShaderRHIParamRef ShaderRHI = GetComputeShader();
		if (bDecomposeCubemaps)
		{
			RWInputCubeAsArray.SetTexture(RHICmdList, ShaderRHI, InputCubeMapTextureRef, InputCubeMapTextureUAVRef);
			check(RWInputCubeAsArray.IsUAVBound());
			SetShaderValue(RHICmdList, ShaderRHI, Offset, InOffset);
		}
		else
		{
			SetTextureParameter(RHICmdList, ShaderRHI, InputCubeMap, DefaultSampler, TStaticSamplerState<SF_Bilinear>::GetRHI(), InputCubeMapTextureRef);
		}
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
		RWInputCubeAsArray.UnsetUAV(RHICmdList, ShaderRHI);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << InputCubeMap;
		Ar << RWInputCubeAsArray;
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
		Ar << Offset;
		return bShaderHasOutdatedParameters;
	}

	static const uint32 kThreadGroupSize = 16;
	static const bool bWriteDepth = (Variant != EProjectCubemapVariant::Color);
	static const bool bWriteLinearDepth = (Variant == EProjectCubemapVariant::ColorAndLinearDepth || Variant == EProjectCubemapVariant::ColorAndLinearDepthToTexture || Variant == EProjectCubemapVariant::ColorAndLinearDepthStacked);
	static const bool bWriteDepthTexture = (Variant == EProjectCubemapVariant::ColorAndLinearDepthToTexture);
	static const bool bWriteStacked = (Variant == EProjectCubemapVariant::ColorAndLinearDepthStacked);
	static const bool bDecomposeCubemaps = (Variant == EProjectCubemapVariant::DecomposeCubemaps||Variant==EProjectCubemapVariant::DecomposeDepth);

private:
	FShaderResourceParameter InputCubeMap;
	FRWShaderParameter RWInputCubeAsArray;
	FShaderResourceParameter DefaultSampler;
	FRWShaderParameter OutputColorTexture; 
	FRWShaderParameter OutputDepthTexture;
	FShaderParameter WorldZToDeviceZTransform;
	FShaderParameter DepthPos;
	FShaderParameter Offset;
};

IMPLEMENT_SHADER_TYPE(, FProjectCubemapCS<EProjectCubemapVariant::Color>, TEXT("/Plugin/RemotePlay/Private/ProjectCubemap.usf"), TEXT("MainCS"), SF_Compute)
IMPLEMENT_SHADER_TYPE(, FProjectCubemapCS<EProjectCubemapVariant::ColorAndDepth>,  TEXT("/Plugin/RemotePlay/Private/ProjectCubemap.usf"), TEXT("MainCS"), SF_Compute)
IMPLEMENT_SHADER_TYPE(, FProjectCubemapCS<EProjectCubemapVariant::ColorAndLinearDepth>,  TEXT("/Plugin/RemotePlay/Private/ProjectCubemap.usf"), TEXT("MainCS"), SF_Compute)
IMPLEMENT_SHADER_TYPE(, FProjectCubemapCS<EProjectCubemapVariant::ColorAndLinearDepthStacked>, TEXT("/Plugin/RemotePlay/Private/ProjectCubemap.usf"), TEXT("MainCS"), SF_Compute)
IMPLEMENT_SHADER_TYPE(, FProjectCubemapCS<EProjectCubemapVariant::ColorAndLinearDepthToTexture>, TEXT("/Plugin/RemotePlay/Private/ProjectCubemap.usf"), TEXT("MainCS"), SF_Compute)
IMPLEMENT_SHADER_TYPE(, FProjectCubemapCS<EProjectCubemapVariant::DecomposeCubemaps>, TEXT("/Plugin/RemotePlay/Private/ProjectCubemap.usf"), TEXT("DecomposeCS"), SF_Compute)
IMPLEMENT_SHADER_TYPE(, FProjectCubemapCS<EProjectCubemapVariant::DecomposeDepth>, TEXT("/Plugin/RemotePlay/Private/ProjectCubemap.usf"), TEXT("DecomposeDepthCS"), SF_Compute)

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

void FEncodePipelineMonoscopic::Initialize(const FRemotePlayEncodeParameters& InParams, struct FRemotePlayContext *context,avs::Queue* InColorQueue, avs::Queue* InDepthQueue)
{
	check(InColorQueue);
	RemotePlayContext = context;
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

void FEncodePipelineMonoscopic::PrepareFrame(FSceneInterface* InScene, UTexture* InSourceTexture)
{
	if (!InScene || !InSourceTexture)
	{
		return;
	}

	const ERHIFeatureLevel::Type FeatureLevel = InScene->GetFeatureLevel();

	auto SourceTarget = CastChecked<UTextureRenderTargetCube>(InSourceTexture);
	FTextureRenderTargetResource* TargetResource = SourceTarget->GameThread_GetRenderTargetResource();
	ENQUEUE_RENDER_COMMAND(RemotePlayPrepareFrame)(
		[this, TargetResource, FeatureLevel](FRHICommandListImmediate& RHICmdList)
		{
			SCOPED_DRAW_EVENT(RHICmdList, RemotePlayEncodePipelineMonoscopicPrepare);
			PrepareFrame_RenderThread(RHICmdList, TargetResource, FeatureLevel);
		}
	);
}


void FEncodePipelineMonoscopic::EncodeFrame(FSceneInterface* InScene, UTexture* InSourceTexture, FTransform& CameraTransform)
{
	if(!InScene || !InSourceTexture)
	{
		return;
	}

	const ERHIFeatureLevel::Type FeatureLevel = InScene->GetFeatureLevel();

	auto SourceTarget = CastChecked<UTextureRenderTargetCube>(InSourceTexture);
	FTextureRenderTargetResource* TargetResource = SourceTarget->GameThread_GetRenderTargetResource();

	ENQUEUE_RENDER_COMMAND(RemotePlayEncodeFrame)(
		[this, CameraTransform](FRHICommandListImmediate& RHICmdList)
		{
			SCOPED_DRAW_EVENT(RHICmdList, RemotePlayEncodePipelineMonoscopic);
			EncodeFrame_RenderThread(RHICmdList, CameraTransform);
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
		avs::SurfaceFormat::NV12 // NV12 is needed for depth encoding
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
	int32 streamWidth = std::max<int32>(Params.FrameWidth, Params.DepthWidth);
	
	int32  streamHeight= Params.FrameHeight + Params.DepthHeight;
	if (Params.bDecomposeCube)
	{
		streamHeight = Params.FrameHeight;
	}
	ColorSurfaceTexture.Texture = RHI.CreateSurfaceTexture(streamWidth, streamHeight, PixelFormat);
	
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
	EncoderParams.preset = avs::VideoPreset::Default;
	EncoderParams.idrInterval = Params.IDRInterval;
	EncoderParams.targetFrameRate = Params.TargetFPS;
	EncoderParams.averageBitrate = Params.AverageBitrate;
	EncoderParams.maxBitrate = Params.MaxBitrate;
	EncoderParams.deferOutput = Params.bDeferOutput;

	Pipeline.Reset(new avs::Pipeline);
	Encoders.SetNum(NumStreams);
	InputSurfaces.SetNum(NumStreams);

	uint8 expectedLag = 2;
	for(uint32_t i=0; i<NumStreams; ++i)
	{ 
		if(!InputSurfaces[i].configure(avsSurfaceBackends[i]))
		{
			UE_LOG(LogRemotePlay, Error, TEXT("Failed to configure input surface node #%d"), i);
			return;
		}
		EncoderParams.inputFormat = avsInputFormats[i];
		if(!Encoders[i].configure(avs::DeviceHandle{avsDeviceType, DeviceHandle}, Params.FrameWidth, Params.FrameHeight, EncoderParams))
		{
			UE_LOG(LogRemotePlay, Error, TEXT("Failed to configure encoder #%d"), i);
			return;
		}

		if(!Pipeline->link({&InputSurfaces[i], &Encoders[i], Outputs[i]}))
		{
			UE_LOG(LogRemotePlay, Error, TEXT("Error configuring the encoding pipeline"));
			return;
		}
		avs::Transform Transform = avs::Transform();
		for (uint8 j = 0; j < expectedLag; ++j)
		{
			Encoders[i].setCameraTransform(Transform);
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
	if (!UnorderedAccessViewRHIRef || !UnorderedAccessViewRHIRef->IsValid() || TargetResource->TextureRHI != SourceCubemapRHI)
	{
		UnorderedAccessViewRHIRef =  RHICmdList.CreateUnorderedAccessView(TargetResource->TextureRHI, 0);
		SourceCubemapRHI = TargetResource->TextureRHI;
	}
	if(DepthQueue)
	{
		{
			DispatchProjectCubemapShader<FProjectCubemapCS<EProjectCubemapVariant::ColorAndLinearDepthToTexture>>(RHICmdList, TargetResource->TextureRHI, UnorderedAccessViewRHIRef, FeatureLevel);
		}
	}
	else
	{
		if (Params.bDecomposeCube)
		{
			DispatchDecomposeCubemapShader(RHICmdList, TargetResource->TextureRHI, UnorderedAccessViewRHIRef, FeatureLevel);
		}
		else if (Params.bStackDepth)
		{
			DispatchProjectCubemapShader<FProjectCubemapCS<EProjectCubemapVariant::ColorAndLinearDepthStacked>>(RHICmdList, TargetResource->TextureRHI, UnorderedAccessViewRHIRef, FeatureLevel);
		}
		else
		{
			DispatchProjectCubemapShader<FProjectCubemapCS<EProjectCubemapVariant::ColorAndDepth>>(RHICmdList, TargetResource->TextureRHI, UnorderedAccessViewRHIRef, FeatureLevel);
		}
	}
}
	
void FEncodePipelineMonoscopic::EncodeFrame_RenderThread(FRHICommandListImmediate& RHICmdList, FTransform CameraTransform)
{
	check(Pipeline.IsValid());
	// The transform of the capture component needs to be sent with the image
	FVector t = CameraTransform.GetTranslation()*0.01f;
	FQuat r = CameraTransform.GetRotation();
	const FVector s = CameraTransform.GetScale3D();
	avs::Transform CamTransform = { t.X, t.Y, t.Z, r.X, r.Y, r.Z, r.W, s.X, s.Y, s.Z };
	avs::ConvertTransform(avs::AxesStandard::UnrealStyle, RemotePlayContext->axesStandard, CamTransform);
	for (auto& Encoder : Encoders)
	{
		Encoder.setCameraTransform(CamTransform);
	}

	if (!Pipeline->process())
	{
		UE_LOG(LogRemotePlay, Warning, TEXT("Encode pipeline processing encountered an error"));
	}
}

template<typename ShaderType>
void FEncodePipelineMonoscopic::DispatchProjectCubemapShader(FRHICommandListImmediate& RHICmdList, FTextureRHIRef TextureRHI, FUnorderedAccessViewRHIRef TextureUAVRHI, ERHIFeatureLevel::Type FeatureLevel)
{
	const uint32 NumThreadGroupsX = Params.FrameWidth / ShaderType::kThreadGroupSize;
	const uint32 NumThreadGroupsY = Params.FrameHeight / ShaderType::kThreadGroupSize;
	const uint32 NumThreadGroupsZ = Params.bDecomposeCube ? 6 : 1;

	TShaderMap<FGlobalShaderType>* GlobalShaderMap = GetGlobalShaderMap(FeatureLevel);

	TShaderMapRef<ShaderType> ComputeShader(GlobalShaderMap);
	ComputeShader->SetParameters(RHICmdList, TextureRHI, TextureUAVRHI,
		ColorSurfaceTexture.Texture, ColorSurfaceTexture.UAV,
		DepthSurfaceTexture.Texture, DepthSurfaceTexture.UAV,
		WorldZToDeviceZTransform, FIntPoint(0, 0), Params.FrameHeight);
	SetComputePipelineState(RHICmdList, GETSAFERHISHADER_COMPUTE(*ComputeShader));
	DispatchComputeShader(RHICmdList, *ComputeShader, NumThreadGroupsX, NumThreadGroupsY, NumThreadGroupsZ);

	ComputeShader->UnsetParameters(RHICmdList);
}

void FEncodePipelineMonoscopic::DispatchDecomposeCubemapShader(FRHICommandListImmediate& RHICmdList, FTextureRHIRef TextureRHI, FUnorderedAccessViewRHIRef TextureUAVRHI, ERHIFeatureLevel::Type FeatureLevel)
{
	TShaderMap<FGlobalShaderType>* GlobalShaderMap = GetGlobalShaderMap(FeatureLevel);
	typedef FProjectCubemapCS<EProjectCubemapVariant::DecomposeCubemaps> ShaderType;
	typedef FProjectCubemapCS<EProjectCubemapVariant::DecomposeDepth> DepthShaderType;
	int W = SourceCubemapRHI->GetSizeXYZ().X;
	{
		const uint32 NumThreadGroupsX = W / ShaderType::kThreadGroupSize;
		const uint32 NumThreadGroupsY = W / ShaderType::kThreadGroupSize;
		const uint32 NumThreadGroupsZ = CubeFace_MAX;

		TShaderMapRef<ShaderType> ComputeShader(GlobalShaderMap);
		ComputeShader->SetParameters(RHICmdList, TextureRHI, TextureUAVRHI,
			ColorSurfaceTexture.Texture, ColorSurfaceTexture.UAV,
			DepthSurfaceTexture.Texture, DepthSurfaceTexture.UAV,
			WorldZToDeviceZTransform, FIntPoint(0, 0), Params.FrameHeight);
		SetComputePipelineState(RHICmdList, GETSAFERHISHADER_COMPUTE(*ComputeShader));
		DispatchComputeShader(RHICmdList, *ComputeShader, NumThreadGroupsX, NumThreadGroupsY, NumThreadGroupsZ);
		ComputeShader->UnsetParameters(RHICmdList);
	}

	{
		const uint32 NumThreadGroupsX = W/2 / ShaderType::kThreadGroupSize;
		const uint32 NumThreadGroupsY = W/2 / ShaderType::kThreadGroupSize;
		const uint32 NumThreadGroupsZ = CubeFace_MAX;
		TShaderMapRef<DepthShaderType> DepthShader(GlobalShaderMap);
		DepthShader->SetParameters(RHICmdList, TextureRHI, TextureUAVRHI,
			ColorSurfaceTexture.Texture, ColorSurfaceTexture.UAV,
			DepthSurfaceTexture.Texture, DepthSurfaceTexture.UAV,
			WorldZToDeviceZTransform, FIntPoint(0, W*2), W*2);
		SetComputePipelineState(RHICmdList, GETSAFERHISHADER_COMPUTE(*DepthShader));
		DispatchComputeShader(RHICmdList, *DepthShader, NumThreadGroupsX, NumThreadGroupsY, NumThreadGroupsZ);
		DepthShader->UnsetParameters(RHICmdList);
	}
	

}

