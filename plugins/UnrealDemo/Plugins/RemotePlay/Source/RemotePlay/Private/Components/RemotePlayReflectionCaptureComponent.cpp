// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	
=============================================================================*/

#include "RemotePlayReflectionCaptureComponent.h"
#include "Engine/TextureRenderTargetCube.h"
#include "RemotePlayRHI.h"

enum class EUpdateReflectionsVariant
{
	FromOriginal,
	FromPreviousMip
};

template<EUpdateReflectionsVariant Variant>
class FUpdateReflectionsCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FUpdateReflectionsCS, Global);
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

	FUpdateReflectionsCS() = default;
	FUpdateReflectionsCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		InputCubeMap.Bind(Initializer.ParameterMap, TEXT("InputCubeMap"));
		DefaultSampler.Bind(Initializer.ParameterMap, TEXT("DefaultSampler"));
		OutputColorTexture.Bind(Initializer.ParameterMap, TEXT("OutputColorTexture"));
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

		if (bWriteDepthTexture)
		{
			OutputDepthTexture.SetTexture(RHICmdList, ShaderRHI, OutputDepthTextureRef, OutputDepthTextureUAVRef);
		}
		if (bWriteLinearDepth)
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
		if (bWriteDepth)
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
		return bShaderHasOutdatedParameters;
	}

	static const uint32 kThreadGroupSize = 16;

private:
	FShaderResourceParameter InputCubeMap;
	FShaderResourceParameter DefaultSampler;
	FRWShaderParameter OutputColorTexture;
	FRWShaderParameter OutputDepthTexture;
	FShaderParameter WorldZToDeviceZTransform;
	FShaderParameter DepthPos;
};

IMPLEMENT_SHADER_TYPE(, FUpdateReflectionsCS<EUpdateReflectionsVariant::FromOriginal>, TEXT("/Plugin/RemotePlay/Private/UpdateReflections.usf"), TEXT("MainCS"), SF_Compute)


URemotePlayReflectionCaptureComponent::URemotePlayReflectionCaptureComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	BoxTransitionDistance = 100;
}

void URemotePlayReflectionCaptureComponent::UpdatePreviewShape()
{
	if (PreviewCaptureBox)
	{
		PreviewCaptureBox->InitBoxExtent(((GetComponentTransform().GetScale3D() - FVector(BoxTransitionDistance)) / GetComponentTransform().GetScale3D()).ComponentMax(FVector::ZeroVector));
	}

	Super::UpdatePreviewShape();
}

float URemotePlayReflectionCaptureComponent::GetInfluenceBoundingRadius() const
{
	return (GetComponentTransform().GetScale3D() + FVector(BoxTransitionDistance)).Size();
}

void URemotePlayReflectionCaptureComponent::Initialize_RenderThread(FRHICommandListImmediate& RHICmdList)
{
	FRemotePlayRHI RHI(RHICmdList);
	FRemotePlayRHI::EDeviceType DeviceType;
	void* DeviceHandle = RHI.GetNativeDevice(DeviceType);

	EPixelFormat PixelFormat = EPixelFormat::PF_R8G8B8A8;


	ColorSurfaceTexture.Texture = RHI.CreateSurfaceTexture(w, Params.FrameHeight + Params.DepthHeight, PixelFormat);

	if (ColorSurfaceTexture.Texture.IsValid())
	{
		ColorSurfaceTexture.UAV = RHI.CreateSurfaceUAV(ColorSurfaceTexture.Texture);
#if PLATFORM_WINDOWS
		if (avsDeviceType == avs::DeviceType::Direct3D11)
		{
			avsSurfaceBackends[0] = new avs::SurfaceDX11(reinterpret_cast<ID3D11Texture2D*>(ColorSurfaceTexture.Texture->GetNativeResource()));
		}
		if (avsDeviceType == avs::DeviceType::Direct3D12)
		{
			avsSurfaceBackends[0] = new avs::SurfaceDX12(reinterpret_cast<ID3D12Resource*>(ColorSurfaceTexture.Texture->GetNativeResource()));
		}
#endif
		if (avsDeviceType == avs::DeviceType::OpenGL)
		{
			// TODO: Implement
		}
	}
	else
	{
		UE_LOG(LogRemotePlay, Error, TEXT("Failed to create encoder color input surface texture"));
		return;
	}
}

void URemotePlayReflectionCaptureComponent::UpdateReflections_RenderThread(
	FRHICommandListImmediate& RHICmdList,
	FTextureRenderTargetResource* TargetResource,
	ERHIFeatureLevel::Type FeatureLevel)
{
	DispatchUpdateReflectionsShader<FUpdateReflectionsCS<EUpdateReflectionsVariant::FromOriginal>>(RHICmdList, TargetResource->TextureRHI,FeatureLevel);
	
}

void URemotePlayReflectionCaptureComponent::UpdateContents(UTextureRenderTargetCube *InSourceTexture, ERHIFeatureLevel::Type FeatureLevel)
{
	auto SourceTarget = CastChecked<UTextureRenderTargetCube>(InSourceTexture);
	FTextureRenderTargetResource* TargetResource = SourceTarget->GameThread_GetRenderTargetResource();

	ENQUEUE_RENDER_COMMAND(RemotePlayCopyReflections)(
		[this, TargetResource, FeatureLevel](FRHICommandListImmediate& RHICmdList)
		{
			//SCOPED_DRAW_EVENT(RHICmdList, RemotePlayReflectionCaptureComponent);
			UpdateReflections_RenderThread(RHICmdList, TargetResource, FeatureLevel);
		}
	);
}

template<typename ShaderType>
void URemotePlayReflectionCaptureComponent::DispatchUpdateReflectionsShader(FRHICommandListImmediate& RHICmdList, FTextureRHIRef SourceTextureRHI, ERHIFeatureLevel::Type FeatureLevel)
{
	int32 OriginalSize = TextureRHI->GetSizeXYZ().X;
	const uint32 NumThreadGroupsX = OriginalSize / ShaderType::kThreadGroupSize;
	const uint32 NumThreadGroupsY = OriginalSize / ShaderType::kThreadGroupSize;

	TShaderMap<FGlobalShaderType>* GlobalShaderMap = GetGlobalShaderMap(FeatureLevel);

	TShaderMapRef<ShaderType> ComputeShader(GlobalShaderMap);
	ComputeShader->SetParameters(RHICmdList, SourceTextureRHI,
		TargetTexture.Texture, TargetTexture.UAV,
);
	SetComputePipelineState(RHICmdList, GETSAFERHISHADER_COMPUTE(*ComputeShader));
	DispatchComputeShader(RHICmdList, *ComputeShader, NumThreadGroupsX, NumThreadGroupsY, 1);
	ComputeShader->UnsetParameters(RHICmdList);
}
