// Copyright 2018 Simul.co
#define WIN32_LEAN_AND_MEAN

#include "EncodePipelineMonoscopic.h"
#include "RemotePlayModule.h"
#include "RemotePlayRHI.h"

#include "RenderingThread.h"
#include "RHIStaticStates.h"
#include "SceneInterface.h"
#include "SceneUtils.h"
#include "RemotePlayMonitor.h"

#include "Engine/TextureRenderTargetCube.h"

#include "Public/RHIDefinitions.h"
#include "Public/GlobalShader.h"
#include "Public/PipelineStateCache.h"
#include "Public/ShaderParameters.h"
#include "Public/ShaderParameterUtils.h"
#include "HAL/UnrealMemory.h"
#include "Containers/DynamicRHIResourceArray.h"
#if PLATFORM_WINDOWS
#include "AllowWindowsPlatformTypes.h"
#include "libavstream/surfaces/surface_dx11.hpp"
#include "libavstream/surfaces/surface_dx12.hpp"
#include "HideWindowsPlatformTypes.h"
#endif
#include <algorithm>

#include <SimulCasterServer/CasterContext.h>
#include <SimulCasterServer/VideoEncodePipeline.h>

DECLARE_FLOAT_COUNTER_STAT(TEXT("RemotePlayEncodePipelineMonoscopic"), Stat_GPU_RemotePlayEncodePipelineMonoscopic, STATGROUP_GPU);

enum class EProjectCubemapVariant
{
	EncodeCameraPosition,
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
	}

	FProjectCubemapCS() = default;
	FProjectCubemapCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		InputCubeMap.Bind(Initializer.ParameterMap, TEXT("InputCubeMap"));
		RWInputCubeAsArray.Bind(Initializer.ParameterMap, TEXT("InputCubeAsArray"));
		InputBlockCullFlagStructBuffer.Bind(Initializer.ParameterMap, TEXT("CullFlags"));
		DefaultSampler.Bind(Initializer.ParameterMap, TEXT("DefaultSampler"));
		OutputColorTexture.Bind(Initializer.ParameterMap, TEXT("OutputColorTexture"));
		Offset.Bind(Initializer.ParameterMap, TEXT("Offset"));
		BlocksPerFaceAcross.Bind(Initializer.ParameterMap, TEXT("BlocksPerFaceAcross"));
		CubemapCameraPositionMetres.Bind(Initializer.ParameterMap, TEXT("CubemapCameraPositionMetres"));
	}
	 
	void SetInputsAndOutputs(
		FRHICommandList& RHICmdList,
		FTextureRHIRef InputCubeMapTextureRef,
		FUnorderedAccessViewRHIRef InputCubeMapTextureUAVRef,
		FShaderResourceViewRHIRef InputBlockCullFlagSRVRef,
		FTexture2DRHIRef OutputColorTextureRef,
		FUnorderedAccessViewRHIRef OutputColorTextureUAVRef)
	{
		FComputeShaderRHIParamRef ShaderRHI = GetComputeShader();
		if (bDecomposeCubemaps)
		{
			RWInputCubeAsArray.SetTexture(RHICmdList, ShaderRHI, InputCubeMapTextureRef, InputCubeMapTextureUAVRef);
			check(RWInputCubeAsArray.IsUAVBound());
			SetSRVParameter(RHICmdList, ShaderRHI, InputBlockCullFlagStructBuffer, InputBlockCullFlagSRVRef);
		}
		else
		{
			SetTextureParameter(RHICmdList, ShaderRHI, InputCubeMap, DefaultSampler, TStaticSamplerState<SF_Bilinear>::GetRHI(), InputCubeMapTextureRef);
		}
		OutputColorTexture.SetTexture(RHICmdList, ShaderRHI, OutputColorTextureRef, OutputColorTextureUAVRef);

	}

	void SetParameters(
		FRHICommandList& RHICmdList,
		const FIntPoint& InOffset,
		const FVector& InCubemapCameraPositionMetres,
		uint32 InBlocksPerFaceAcross)
	{
		FComputeShaderRHIParamRef ShaderRHI = GetComputeShader();
		SetShaderValue(RHICmdList, ShaderRHI, Offset, InOffset);
		SetShaderValue(RHICmdList, ShaderRHI, CubemapCameraPositionMetres, InCubemapCameraPositionMetres);
		SetShaderValue(RHICmdList, ShaderRHI, BlocksPerFaceAcross, InBlocksPerFaceAcross);
	}

	void UnsetParameters(FRHICommandList& RHICmdList)
	{
		FComputeShaderRHIParamRef ShaderRHI = GetComputeShader();
		OutputColorTexture.UnsetUAV(RHICmdList, ShaderRHI);
		RWInputCubeAsArray.UnsetUAV(RHICmdList, ShaderRHI);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << InputCubeMap;
		Ar << RWInputCubeAsArray;
		Ar << InputBlockCullFlagStructBuffer;
		Ar << DefaultSampler;
		Ar << OutputColorTexture;
		Ar << Offset;
		Ar << BlocksPerFaceAcross;
		Ar << CubemapCameraPositionMetres;
		return bShaderHasOutdatedParameters;
	}

	static const uint32 kThreadGroupSize = 16;
	static const bool bWriteDepth = true;
	static const bool bWriteLinearDepth =true;
	static const bool bDecomposeCubemaps = (Variant == EProjectCubemapVariant::DecomposeCubemaps||Variant==EProjectCubemapVariant::DecomposeDepth);

private:
	FShaderResourceParameter InputCubeMap;
	FRWShaderParameter RWInputCubeAsArray;
	FShaderResourceParameter InputBlockCullFlagStructBuffer;
	FShaderResourceParameter DefaultSampler;
	FRWShaderParameter OutputColorTexture; 
	FShaderParameter CubemapCameraPositionMetres;
	FShaderParameter Offset; 
	FShaderParameter BlocksPerFaceAcross;
};

IMPLEMENT_SHADER_TYPE(, FProjectCubemapCS<EProjectCubemapVariant::EncodeCameraPosition>, TEXT("/Plugin/RemotePlay/Private/ProjectCubemap.usf"), TEXT("EncodeCameraPositionCS"), SF_Compute)
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

void FEncodePipelineMonoscopic::Initialise(const FUnrealCasterEncoderSettings& InSettings, SCServer::CasterContext* context, ARemotePlayMonitor* InMonitor)
{
	check(context->ColorQueue);

	CasterContext = context;
	Settings = InSettings;
	Monitor = InMonitor;
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
}

void FEncodePipelineMonoscopic::CullHiddenCubeSegments(FSceneInterface* InScene, SCServer::CameraInfo& CameraInfo, int32 FaceSize, uint32 Divisor)
{
	const ERHIFeatureLevel::Type FeatureLevel = InScene->GetFeatureLevel();

	ENQUEUE_RENDER_COMMAND(RemotePlayCullHiddenCubeSegments)(
		[this, CameraInfo, FeatureLevel, FaceSize, Divisor](FRHICommandListImmediate& RHICmdList)
		{
			SCOPED_DRAW_EVENT(RHICmdList, RemotePlayEncodePipelineCullHiddenCubeSegments);
			CullHiddenCubeSegments_RenderThread(RHICmdList, FeatureLevel, CameraInfo, FaceSize, Divisor);
		}
	);
}

void FEncodePipelineMonoscopic::PrepareFrame(FSceneInterface* InScene, UTexture* InSourceTexture, FTransform& CameraTransform, const TArray<bool>& BlockIntersectionFlags)
{
	if (!InScene || !InSourceTexture)
	{
		return;
	}

	const ERHIFeatureLevel::Type FeatureLevel = InScene->GetFeatureLevel();

	auto SourceTarget = CastChecked<UTextureRenderTargetCube>(InSourceTexture);
	FTextureRenderTargetResource* TargetResource = SourceTarget->GameThread_GetRenderTargetResource();
	ENQUEUE_RENDER_COMMAND(RemotePlayPrepareFrame)(
		[this, CameraTransform, BlockIntersectionFlags, TargetResource, FeatureLevel](FRHICommandListImmediate& RHICmdList)
		{
			SCOPED_DRAW_EVENT(RHICmdList, RemotePlayEncodePipelineMonoscopicPrepare);
			PrepareFrame_RenderThread(RHICmdList, TargetResource, FeatureLevel, CameraTransform.GetTranslation(), BlockIntersectionFlags);
		}
	);
}


void FEncodePipelineMonoscopic::EncodeFrame(FSceneInterface* InScene, UTexture* InSourceTexture, FTransform& CameraTransform, bool forceIDR)
{
	if(!InScene || !InSourceTexture)
	{
		return;
	}
	// only proceed if network is ready to stream.
	const ERHIFeatureLevel::Type FeatureLevel = InScene->GetFeatureLevel();

	auto SourceTarget = CastChecked<UTextureRenderTargetCube>(InSourceTexture);
	FTextureRenderTargetResource* TargetResource = SourceTarget->GameThread_GetRenderTargetResource();

	ENQUEUE_RENDER_COMMAND(RemotePlayEncodeFrame)(
		[this, CameraTransform, forceIDR](FRHICommandListImmediate& RHICmdList)
		{
			SCOPED_DRAW_EVENT(RHICmdList, RemotePlayEncodePipelineMonoscopic);
			EncodeFrame_RenderThread(RHICmdList, CameraTransform, forceIDR);
		}
	);
}
	
void FEncodePipelineMonoscopic::Initialize_RenderThread(FRHICommandListImmediate& RHICmdList)
{
	FRemotePlayRHI RHI(RHICmdList);
	FRemotePlayRHI::EDeviceType DeviceType;
	void* DeviceHandle = RHI.GetNativeDevice(DeviceType);

	SCServer::GraphicsDeviceType CasterDeviceType;
	avs::SurfaceBackendInterface* avsSurfaceBackends[2] = { nullptr };

	EPixelFormat PixelFormat;
	if (Monitor->bUse10BitEncoding)
	{
		PixelFormat = EPixelFormat::PF_R16G16B16A16_UNORM;
	}
	else
	{
		PixelFormat = EPixelFormat::PF_R8G8B8A8;
	}

	switch(DeviceType)
	{
	case FRemotePlayRHI::EDeviceType::Direct3D11:
		CasterDeviceType = SCServer::GraphicsDeviceType::Direct3D11;
		break;
	case FRemotePlayRHI::EDeviceType::Direct3D12:
		CasterDeviceType = SCServer::GraphicsDeviceType::Direct3D12;
		break;
	case FRemotePlayRHI::EDeviceType::OpenGL:
		CasterDeviceType = SCServer::GraphicsDeviceType::OpenGL;
		break;
	default:
		UE_LOG(LogRemotePlay, Error, TEXT("Failed to obtain native device handle"));
		return; 
	} 
	// Roderick: we create a DOUBLE-HEIGHT texture, and encode colour in the top half, depth in the bottom.
	int32 streamWidth;
	int32  streamHeight;
	if (Settings.bDecomposeCube)
	{
		streamWidth = Settings.FrameWidth;
		streamHeight = Settings.FrameHeight;
	}
	else
	{
		streamWidth = std::max<int32>(Settings.FrameWidth, Settings.DepthWidth);
		streamHeight = Settings.FrameHeight + Settings.DepthHeight;
	}
	ColorSurfaceTexture.Texture = RHI.CreateSurfaceTexture(streamWidth, streamHeight, PixelFormat);
	D3D12_RESOURCE_DESC desc = ((ID3D12Resource*)ColorSurfaceTexture.Texture->GetNativeResource())->GetDesc();

	if(ColorSurfaceTexture.Texture.IsValid())
	{
		ColorSurfaceTexture.UAV = RHI.CreateSurfaceUAV(ColorSurfaceTexture.Texture);
	}
	else
	{
		UE_LOG(LogRemotePlay, Error, TEXT("Failed to create encoder color input surface texture"));
		return;
	}

	Pipeline.Reset(new SCServer::VideoEncodePipeline);
	
	auto CasterSettings = Settings.GetAsCasterEncoderSettings();
	SCServer::VideoEncodeParams params;
	params.encodeWidth = CasterSettings.frameWidth;
	params.encodeHeight = CasterSettings.frameHeight;
	params.deviceHandle = DeviceHandle;
	params.deviceType = CasterDeviceType;
	params.inputSurfaceResource = ColorSurfaceTexture.Texture->GetNativeResource();
	params.output = CasterContext->ColorQueue.get();

	Pipeline->initialize(*Monitor->GetCasterSettings(), params);
}
	
void FEncodePipelineMonoscopic::Release_RenderThread(FRHICommandListImmediate& RHICmdList)
{
	Pipeline.Reset();

	ColorSurfaceTexture.Texture.SafeRelease();
	ColorSurfaceTexture.UAV.SafeRelease();

	DepthSurfaceTexture.Texture.SafeRelease();
	DepthSurfaceTexture.UAV.SafeRelease();
} 

void FEncodePipelineMonoscopic::CullHiddenCubeSegments_RenderThread(FRHICommandListImmediate& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, SCServer::CameraInfo CameraInfo, int32 FaceSize, uint32 Divisor)
{
	// Aidan: Currently not going to do this on GPU so this function is unused
	// We will do this on cpu on game thread instead because we can share the output with the capture component to cull faces from rendering.
}

void FEncodePipelineMonoscopic::PrepareFrame_RenderThread(
	FRHICommandListImmediate& RHICmdList,
	FTextureRenderTargetResource* TargetResource,
	ERHIFeatureLevel::Type FeatureLevel,
	FVector CameraPosition,
	TArray<bool> BlockIntersectionFlags)
{
	if (!UnorderedAccessViewRHIRef || !UnorderedAccessViewRHIRef->IsValid() || TargetResource->TextureRHI != SourceCubemapRHI)
	{
		UnorderedAccessViewRHIRef =  RHICmdList.CreateUnorderedAccessView(TargetResource->TextureRHI, 0);
		SourceCubemapRHI = TargetResource->TextureRHI;
	}
	{
		if (Settings.bDecomposeCube)
		{
			DispatchDecomposeCubemapShader(RHICmdList, TargetResource->TextureRHI, UnorderedAccessViewRHIRef, FeatureLevel, CameraPosition, BlockIntersectionFlags);
		}
	}
}
	
void FEncodePipelineMonoscopic::EncodeFrame_RenderThread(FRHICommandListImmediate& RHICmdList, FTransform CameraTransform, bool forceIDR)
{
	check(Pipeline.IsValid());

	// The transform of the capture component needs to be sent with the image
	FVector t = CameraTransform.GetTranslation()*0.01f;
	FQuat r = CameraTransform.GetRotation();
	const FVector s = CameraTransform.GetScale3D();
	avs::Transform CamTransform; 
	CamTransform.position = { t.X, t.Y, t.Z };
	CamTransform.rotation = { r.X, r.Y, r.Z, r.W };
	CamTransform.scale = { s.X, s.Y, s.Z };

	avs::ConvertTransform(avs::AxesStandard::UnrealStyle, CasterContext->axesStandard, CamTransform);

	SCServer::Result result = Pipeline->process(CamTransform, forceIDR);
	if (!result)
	{
		UE_LOG(LogRemotePlay, Warning, TEXT("Encode pipeline processing encountered an error"));
	}
}

template<typename ShaderType>
void FEncodePipelineMonoscopic::DispatchProjectCubemapShader(FRHICommandListImmediate& RHICmdList, FTextureRHIRef TextureRHI, FUnorderedAccessViewRHIRef TextureUAVRHI, ERHIFeatureLevel::Type FeatureLevel)
{
	const uint32 NumThreadGroupsX = Settings.FrameWidth / ShaderType::kThreadGroupSize;
	const uint32 NumThreadGroupsY = Settings.FrameHeight / ShaderType::kThreadGroupSize;
	const uint32 NumThreadGroupsZ = Settings.bDecomposeCube ? 6 : 1;

	TShaderMap<FGlobalShaderType>* GlobalShaderMap = GetGlobalShaderMap(FeatureLevel);

	TShaderMapRef<ShaderType> ComputeShader(GlobalShaderMap);
	ComputeShader->SetParameters(RHICmdList, TextureRHI, TextureUAVRHI,
		ColorSurfaceTexture.Texture, ColorSurfaceTexture.UAV,
		 FIntPoint(0, 0));
	SetComputePipelineState(RHICmdList, GETSAFERHISHADER_COMPUTE(*ComputeShader));
	DispatchComputeShader(RHICmdList, *ComputeShader, NumThreadGroupsX, NumThreadGroupsY, NumThreadGroupsZ);

	ComputeShader->UnsetParameters(RHICmdList);
}

void FEncodePipelineMonoscopic::DispatchDecomposeCubemapShader(FRHICommandListImmediate& RHICmdList, FTextureRHIRef TextureRHI
	, FUnorderedAccessViewRHIRef TextureUAVRHI, ERHIFeatureLevel::Type FeatureLevel
	,FVector CameraPosition, const TArray<bool>& BlockIntersectionFlags)
{
	FVector  t = CameraPosition *0.01f;
	avs::vec3 pos_m ={t.X,t.Y,t.Z};
	avs::ConvertPosition(avs::AxesStandard::UnrealStyle, CasterContext->axesStandard, pos_m);
	const FVector &CameraPositionMetres =*((const FVector*)&pos_m);
	TShaderMap<FGlobalShaderType>* GlobalShaderMap = GetGlobalShaderMap(FeatureLevel);
	typedef FProjectCubemapCS<EProjectCubemapVariant::DecomposeCubemaps> ShaderType;
	typedef FProjectCubemapCS<EProjectCubemapVariant::DecomposeDepth> DepthShaderType;
	typedef FProjectCubemapCS<EProjectCubemapVariant::EncodeCameraPosition> EncodeCameraPositionShaderType;
	
	TResourceArray<FShaderFlag> BlockCullFlags;

	for (auto& Flag : BlockIntersectionFlags)
	{
		BlockCullFlags.Add({ Flag, 0, 0, 0 });
	}

	FRHIResourceCreateInfo CreateInfo;
	CreateInfo.ResourceArray = &BlockCullFlags;

	FStructuredBufferRHIRef BlockCullFlagSB = RHICreateStructuredBuffer(
		sizeof(FShaderFlag),
		BlockCullFlags.Num() * sizeof(FShaderFlag),
		BUF_ShaderResource,
		CreateInfo
	);

	FShaderResourceViewRHIRef BlockCullFlagSRV = RHICreateShaderResourceView(BlockCullFlagSB);

	// This is the number of segments each cube face is split into for culling
	uint32 BlocksPerFaceAcross = (uint32)FMath::Sqrt(BlockCullFlags.Num() / 6);

	int W = SourceCubemapRHI->GetSizeXYZ().X;
	{
		const uint32 NumThreadGroupsX = W / ShaderType::kThreadGroupSize;
		const uint32 NumThreadGroupsY = W / ShaderType::kThreadGroupSize;
		const uint32 NumThreadGroupsZ = CubeFace_MAX;

		TShaderMapRef<ShaderType> ComputeShader(GlobalShaderMap);
		ComputeShader->SetInputsAndOutputs(RHICmdList, TextureRHI, TextureUAVRHI,
			BlockCullFlagSRV, ColorSurfaceTexture.Texture, ColorSurfaceTexture.UAV);
		ComputeShader->SetParameters(RHICmdList, FIntPoint(0, 0), CameraPositionMetres, BlocksPerFaceAcross);
		SetComputePipelineState(RHICmdList, GETSAFERHISHADER_COMPUTE(*ComputeShader));
		DispatchComputeShader(RHICmdList, *ComputeShader, NumThreadGroupsX, NumThreadGroupsY, NumThreadGroupsZ);
		ComputeShader->UnsetParameters(RHICmdList);
	}

	{
		const uint32 NumThreadGroupsX = W/2 / ShaderType::kThreadGroupSize;
		const uint32 NumThreadGroupsY = W/2 / ShaderType::kThreadGroupSize;
		const uint32 NumThreadGroupsZ = CubeFace_MAX;
		TShaderMapRef<DepthShaderType> DepthShader(GlobalShaderMap);
		DepthShader->SetInputsAndOutputs(RHICmdList, TextureRHI, TextureUAVRHI,
			BlockCullFlagSRV, ColorSurfaceTexture.Texture, ColorSurfaceTexture.UAV);
		DepthShader->SetParameters(RHICmdList, FIntPoint(0, W * 2), CameraPositionMetres, BlocksPerFaceAcross);
		SetComputePipelineState(RHICmdList, GETSAFERHISHADER_COMPUTE(*DepthShader));
		DispatchComputeShader(RHICmdList, *DepthShader, NumThreadGroupsX, NumThreadGroupsY, NumThreadGroupsZ);
		DepthShader->UnsetParameters(RHICmdList);
	}

	{
		const uint32 NumThreadGroupsX = 4;
		const uint32 NumThreadGroupsY = 1;
		const uint32 NumThreadGroupsZ = 1;
		TShaderMapRef<EncodeCameraPositionShaderType> EncodePosShader(GlobalShaderMap);
		EncodePosShader->SetInputsAndOutputs(RHICmdList, TextureRHI, TextureUAVRHI,
			BlockCullFlagSRV, ColorSurfaceTexture.Texture, ColorSurfaceTexture.UAV);
		EncodePosShader->SetParameters(RHICmdList, FIntPoint(W*3-(32*4), W * 3-(3*8)), CameraPositionMetres, BlocksPerFaceAcross);
		SetComputePipelineState(RHICmdList, GETSAFERHISHADER_COMPUTE(*EncodePosShader));
		DispatchComputeShader(RHICmdList, *EncodePosShader, NumThreadGroupsX, NumThreadGroupsY, NumThreadGroupsZ);
		EncodePosShader->UnsetParameters(RHICmdList);
	}
	
}

