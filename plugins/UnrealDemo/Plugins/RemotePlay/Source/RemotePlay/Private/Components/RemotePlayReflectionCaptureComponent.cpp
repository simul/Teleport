// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	
=============================================================================*/

#include "RemotePlayReflectionCaptureComponent.h"
#include "Engine/TextureRenderTargetCube.h"
#include "Renderer/Private/ScenePrivate.h"
#include "SceneManagement.h"
#include "RemotePlayRHI.h"
#include "PixelShaderUtils.h"
#include "Engine/TextureRenderTargetCube.h"
#include <algorithm>
#include "Pipelines/EncodePipelineInterface.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/SkyLight.h"
#include "Components/SkyLightComponent.h"

enum class EUpdateReflectionsVariant
{
	NoSource,
	FromOriginal,
	FromPreviousMip,
	WriteToStream
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
		RWOutputTexture.Bind(Initializer.ParameterMap, TEXT("OutputTexture"));
		CubeFace.Bind(Initializer.ParameterMap, TEXT("CubeFace"));
		DirLightCount.Bind(Initializer.ParameterMap, TEXT("DirLightCount"));
		DirLightStructBuffer.Bind(Initializer.ParameterMap, TEXT("DirLights"));

		InputCubemapAsArrayTexture.Bind(Initializer.ParameterMap, TEXT("InputCubemapAsArrayTexture"));
		RWStreamOutputTexture.Bind(Initializer.ParameterMap, TEXT("StreamOutputTexture"));
		check(RWOutputTexture.IsUAVBound()|| RWStreamOutputTexture.IsUAVBound());
	}

	void SetParameters(
		FRHICommandList& RHICmdList,
		FTextureCubeRHIRef InputCubeMapTextureRef,
		FTextureCubeRHIRef OutputColorTextureRef,
		FUnorderedAccessViewRHIRef OutputColorTextureUAVRef,
		uint32 InCubeFace,
		uint32 InDirLightCount,
		FShaderResourceViewRHIRef DirLightsShaderResourceViewRef)
	{
		FComputeShaderRHIParamRef ShaderRHI = GetComputeShader();

		if(InputCubeMapTextureRef)
			SetTextureParameter(RHICmdList, ShaderRHI, InputCubeMap, DefaultSampler, TStaticSamplerState<SF_Bilinear>::GetRHI(), InputCubeMapTextureRef);
		RWOutputTexture.SetTexture(RHICmdList, ShaderRHI, OutputColorTextureRef, OutputColorTextureUAVRef);

		SetShaderValue(RHICmdList, ShaderRHI, CubeFace, InCubeFace);
		SetShaderValue(RHICmdList, ShaderRHI, DirLightCount, InDirLightCount);
		SetSRVParameter(RHICmdList, ShaderRHI, DirLightStructBuffer, DirLightsShaderResourceViewRef);
	}

	void SetStreamParameters(
		FRHICommandList& RHICmdList,
		FTextureCubeRHIRef InputColorTextureRef,
		FUnorderedAccessViewRHIRef InputColorTextureUAVRef,
		FTexture2DRHIRef OutputColorTextureRef,
		FUnorderedAccessViewRHIRef OutputColorTextureUAVRef
		)
	{
		FComputeShaderRHIParamRef ShaderRHI = GetComputeShader();
		RWOutputTexture.SetTexture(RHICmdList, ShaderRHI, InputColorTextureRef, InputColorTextureUAVRef);
		RWStreamOutputTexture.SetTexture(RHICmdList, ShaderRHI, OutputColorTextureRef, OutputColorTextureUAVRef);
	}

	void UnsetParameters(FRHICommandList& RHICmdList)
	{
		FComputeShaderRHIParamRef ShaderRHI = GetComputeShader();
		RWOutputTexture.UnsetUAV(RHICmdList, ShaderRHI);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << InputCubeMap;
		Ar << DefaultSampler;
		Ar << RWOutputTexture;
		Ar << InputCubemapAsArrayTexture;
		Ar << RWStreamOutputTexture;
		Ar << CubeFace;
		Ar << DirLightCount;
		Ar << DirLightStructBuffer;
		return bShaderHasOutdatedParameters;
	}

	static const uint32 kThreadGroupSize = 16;
	//static const bool bWriteStacked = (Variant == EProjectCubemapVariant::ColorAndLinearDepthStacked);

private:
	FShaderResourceParameter InputCubeMap;
	FShaderResourceParameter DefaultSampler;
	FRWShaderParameter RWOutputTexture;
	FShaderResourceParameter InputCubemapAsArrayTexture;
	FRWShaderParameter RWStreamOutputTexture;
	FShaderParameter CubeFace;
	FShaderParameter DirLightCount;
	FShaderResourceParameter DirLightStructBuffer;
};
/** Pixel shader used for filtering a mip. */
/*
class FUpdateReflectionsPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FUpdateReflectionsPS, Global);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	FUpdateReflectionsPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{
		CubeFace.Bind(Initializer.ParameterMap, TEXT("CubeFace"));
		MipIndex.Bind(Initializer.ParameterMap, TEXT("MipIndex"));
		NumMips.Bind(Initializer.ParameterMap, TEXT("NumMips"));
		SourceTexture.Bind(Initializer.ParameterMap, TEXT("SourceTexture"));
		SourceTextureSampler.Bind(Initializer.ParameterMap, TEXT("SourceTextureSampler"));
	}
	FUpdateReflectionsPS() {}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << CubeFace;
		Ar << MipIndex;
		Ar << NumMips;
		Ar << SourceTexture;
		Ar << SourceTextureSampler;
		return bShaderHasOutdatedParameters;
	}

	FShaderParameter CubeFace;
	FShaderParameter MipIndex;
	FShaderParameter NumMips;
	FShaderResourceParameter SourceTexture;
	FShaderResourceParameter SourceTextureSampler;
};
*/
IMPLEMENT_SHADER_TYPE(, FUpdateReflectionsCS<EUpdateReflectionsVariant::NoSource>, TEXT("/Plugin/RemotePlay/Private/UpdateReflections.usf"), TEXT("NoSourceCS"), SF_Compute)
IMPLEMENT_SHADER_TYPE(, FUpdateReflectionsCS<EUpdateReflectionsVariant::FromOriginal>, TEXT("/Plugin/RemotePlay/Private/UpdateReflections.usf"), TEXT("FromCubemapCS"), SF_Compute)
IMPLEMENT_SHADER_TYPE(, FUpdateReflectionsCS<EUpdateReflectionsVariant::FromPreviousMip>, TEXT("/Plugin/RemotePlay/Private/UpdateReflections.usf"), TEXT("FromMipCS"), SF_Compute)
IMPLEMENT_SHADER_TYPE(, FUpdateReflectionsCS<EUpdateReflectionsVariant::WriteToStream>, TEXT("/Plugin/RemotePlay/Private/UpdateReflections.usf"), TEXT("WriteToStreamCS"), SF_Compute)

//IMPLEMENT_SHADER_TYPE(, FUpdateReflectionsPS, TEXT("/Plugin/RemotePlay/Private/UpdateReflections.usf"), TEXT("UpdateReflectionsPS"), SF_Pixel);

// Duplicated from ReflectionEnvironment.cpp.
int32 FindOrAllocateCubemapIndex(FScene* Scene, const UReflectionCaptureComponent* Component)
{
	int32 CubemapIndex = -1;

	// Try to find an existing capture index for this component
	const FCaptureComponentSceneState* CaptureSceneStatePtr = Scene->ReflectionSceneData.AllocatedReflectionCaptureState.Find(Component);

	if (CaptureSceneStatePtr)
	{
		CubemapIndex = CaptureSceneStatePtr->CubemapIndex;
	}
	else
	{
		// Reuse a freed index if possible
		CubemapIndex = Scene->ReflectionSceneData.CubemapArraySlotsUsed.FindAndSetFirstZeroBit();
		if (CubemapIndex == INDEX_NONE)
		{
			// If we didn't find a free index, allocate a new one from the CubemapArraySlotsUsed bitfield
			CubemapIndex = Scene->ReflectionSceneData.CubemapArraySlotsUsed.Num();
			if (CubemapIndex >= Scene->ReflectionSceneData.CubemapArray.GetMaxCubemaps())
				return -1;
			Scene->ReflectionSceneData.CubemapArraySlotsUsed.Add(true);
		}

		Scene->ReflectionSceneData.AllocatedReflectionCaptureState.Add(Component, FCaptureComponentSceneState(CubemapIndex));
		Scene->ReflectionSceneData.AllocatedReflectionCaptureStateHasChanged = true;

		check(CubemapIndex < GMaxNumReflectionCaptures);
	}

	check(CubemapIndex >= 0);
	return CubemapIndex;
}

URemotePlayReflectionCaptureComponent::URemotePlayReflectionCaptureComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bAttached = false;
	BoxTransitionDistance = 100;
	Mobility = EComponentMobility::Movable;
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bTickEvenWhenPaused = true;
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
	FRHIResourceCreateInfo CreateInfo;
	const int32 NumMips = FMath::CeilLogTwo(128) + 1;
	
	ReflectionCubeTexture.TextureCubeRHIRef = RHICmdList.CreateTextureCube(128, PF_FloatRGBA, NumMips, TexCreate_UAV, CreateInfo);
	//RHICmdList.GetResourceInfo(ReflectionCubeTexture.TextureCubeRHIRef, CreateInfo);
	for (int i = 0; i < NumMips; i++)
	{
		ReflectionCubeTexture.UnorderedAccessViewRHIRefs[i]
			= RHICmdList.CreateUnorderedAccessView(ReflectionCubeTexture.TextureCubeRHIRef, i);
	}
	
	
}
void URemotePlayReflectionCaptureComponent::Release_RenderThread(FRHICommandListImmediate& RHICmdList)
{
	if(ReflectionCubeTexture.TextureCubeRHIRef)
		ReflectionCubeTexture.TextureCubeRHIRef->Release();
	const int32 NumMips = FMath::CeilLogTwo(128) + 1;
	for (int i = 0; i < NumMips; i++)
	{
		if (ReflectionCubeTexture.UnorderedAccessViewRHIRefs[i])
			ReflectionCubeTexture.UnorderedAccessViewRHIRefs[i]->Release();
	}
}


void URemotePlayReflectionCaptureComponent::UpdateReflections_RenderThread(
	FRHICommandListImmediate& RHICmdList, FScene *Scene,
	UTextureRenderTargetCube *InSourceTexture,
	ERHIFeatureLevel::Type FeatureLevel)
{
	if (!ReflectionCubeTexture.TextureCubeRHIRef)
		Initialize_RenderThread(RHICmdList);
	FTextureRenderTargetCubeResource* SourceCubeResource = nullptr;
	if(InSourceTexture)
		SourceCubeResource =static_cast<FTextureRenderTargetCubeResource*>(InSourceTexture->GetRenderTargetResource());
	const int32 EffectiveTopMipSize = 128;

	const int32 NumMips =  FMath::CeilLogTwo(EffectiveTopMipSize) + 1;

	const int32 CaptureIndex = FindOrAllocateCubemapIndex(Scene, this);
//	FTextureRHIParamRef CubemapArray;
	FTextureRHIRef TargetResource;
	if (OverrideTexture)
	{
		TargetResource = OverrideTexture->GetRenderTargetResource()->TextureRHI;
	}
	else
	{
		if (CaptureIndex>=0&&Scene&&Scene->ReflectionSceneData.CubemapArray.GetCubemapSize())
		{
			FSceneRenderTargetItem &rt = Scene->ReflectionSceneData.CubemapArray.GetRenderTarget();
			if (rt.IsValid())
				TargetResource = rt.TargetableTexture;
		}
	}
/*	if (Scene->ReflectionSceneData.CubemapArray.IsValid() &&
		Scene->ReflectionSceneData.CubemapArray.GetRenderTarget().IsValid())
	{
		CubemapArray = Scene->ReflectionSceneData.CubemapArray.GetRenderTarget().ShaderResourceTexture;
	}
	else
	{
		return;
	}*/
	//DispatchUpdateReflectionsShader<FUpdateReflectionsCS<EUpdateReflectionsVariant::FromOriginal>>(
	//	RHICmdList, InSourceTexture->TextureReference.TextureReferenceRHI.GetReference()->GetTextureReference(), Target_UAV,FeatureLevel);

	//FSceneRenderTargetItem& DestCube = Scene->ReflectionSceneData.CubemapArray.GetRenderTarget();


	SCOPED_DRAW_EVENT(RHICmdList, UpdateReflections);

	auto* ShaderMap = GetGlobalShaderMap(FeatureLevel);

	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
	GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();

	TResourceArray<FShaderDirectionalLight> ShaderDirLights;
	ShaderDirLightsQueue.Dequeue(ShaderDirLights);

	FRHIResourceCreateInfo CreateInfo;
	CreateInfo.ResourceArray = &ShaderDirLights;

	FStructuredBufferRHIRef DirLightSB = RHICreateStructuredBuffer(
		sizeof(FShaderDirectionalLight),
		ShaderDirLights.Num() * sizeof(FShaderDirectionalLight),
		BUF_ShaderResource,
		CreateInfo
	);

	FShaderResourceViewRHIRef DirLightSRV = RHICreateShaderResourceView(DirLightSB);

	// Downsample all the mips, each one reads from the mip above it
	FGlobalShader *Shader = nullptr;
	TShaderMapRef<FUpdateReflectionsCS<EUpdateReflectionsVariant::FromOriginal>> ComputeShader(ShaderMap);
	Shader = ComputeShader.operator*();
	for (int32 MipIndex = 0; MipIndex < NumMips; MipIndex++)
	{
		const int32 MipSize = 1 << (NumMips - MipIndex - 1);
		for (int32 CubeFace = 0; CubeFace < CubeFace_MAX; CubeFace++)
		{
			//RHICmdList.CopyToResolveTarget(InSourceTexture->Resource->TextureRHI, DestCube.ShaderResourceTexture, FResolveParams(FResolveRect(), (ECubeFace)CubeFace, MipIndex, 0, CaptureIndex));

			//RHICmdList.TransitionResources(EResourceTransitionAccess::EWritable, ReflectionCubeTexture.TextureCubeRHIRef.GetReference(), 1);
			ComputeShader->SetParameters(RHICmdList, SourceCubeResource ? SourceCubeResource->GetTextureRHI() : nullptr,
				ReflectionCubeTexture.TextureCubeRHIRef,
				ReflectionCubeTexture.UnorderedAccessViewRHIRefs[MipIndex],
				CubeFace,
				ShaderDirLights.Num(),
				DirLightSRV);
			SetComputePipelineState(RHICmdList, GETSAFERHISHADER_COMPUTE(Shader));
			const uint32 NumThreadGroupsX = MipSize / 16;
			const uint32 NumThreadGroupsY = MipSize / 16;
			DispatchComputeShader(RHICmdList, Shader, NumThreadGroupsX, NumThreadGroupsY, 1);
			ComputeShader->UnsetParameters(RHICmdList);
			// Now copy this face of this cube into the cubemap array.
		}
	}
	if (TargetResource)
	{
		for (int32 MipIndex = 0; MipIndex < std::min(NumMips, (int32)TargetResource->GetNumMips()); MipIndex++)
		{
			for (int32 CubeFace = 0; CubeFace < CubeFace_MAX; CubeFace++)
			{
				FResolveParams ResolveParams;
				ResolveParams.Rect = FResolveRect();
				ResolveParams.SourceArrayIndex = 0;
				ResolveParams.DestArrayIndex = CaptureIndex >= 0 ? CaptureIndex : 0;
				ResolveParams.CubeFace = (ECubeFace)CubeFace;
				ResolveParams.MipIndex = MipIndex;
				RHICmdList.CopyToResolveTarget(ReflectionCubeTexture.TextureCubeRHIRef
					, TargetResource, ResolveParams);
			}
		}
	}
	//if(CubemapArray&&CubemapArray->IsValid())
//		RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, &CubemapArray, 1);

	//Release the resources
	DirLightSRV->Release();
	DirLightSB->Release();
}

// write the reflections to the UAV of the output video stream.
void URemotePlayReflectionCaptureComponent::WriteReflections_RenderThread(FRHICommandListImmediate& RHICmdList, FScene *Scene, FSurfaceTexture *TargetSurfaceTexture, ERHIFeatureLevel::Type FeatureLevel)
{

	SCOPED_DRAW_EVENT(RHICmdList, WriteReflections);

	auto* ShaderMap = GetGlobalShaderMap(FeatureLevel);

	TShaderMapRef<FUpdateReflectionsCS<EUpdateReflectionsVariant::WriteToStream>> ComputeShader(ShaderMap);
	const int32 EffectiveTopMipSize	=128;
	const int32 NumMips				=FMath::CeilLogTwo(EffectiveTopMipSize) + 1;
	SCOPED_DRAW_EVENT(RHICmdList, WriteReflections);

	FShader *Shader = ComputeShader.operator*();
	for (int32 MipIndex = 0; MipIndex < NumMips; MipIndex++)
	{
		const int32 MipSize = 1 << (NumMips - MipIndex - 1);
		for (int32 CubeFace = 0; CubeFace < CubeFace_MAX; CubeFace++)
		{
			ComputeShader->SetStreamParameters(RHICmdList
				,ReflectionCubeTexture.TextureCubeRHIRef
				,ReflectionCubeTexture.UnorderedAccessViewRHIRefs[MipIndex]
				,TargetSurfaceTexture->Texture
				,TargetSurfaceTexture->UAV
				);
			SetComputePipelineState(RHICmdList, GETSAFERHISHADER_COMPUTE(Shader));
			const uint32 NumThreadGroupsX = MipSize / 16;
			const uint32 NumThreadGroupsY = MipSize / 16;
			DispatchComputeShader(RHICmdList, Shader, NumThreadGroupsX, NumThreadGroupsY, 1);
			ComputeShader->UnsetParameters(RHICmdList);
			// Now copy this face of this cube into the cubemap array.
		}
	}
}

void URemotePlayReflectionCaptureComponent::Initialize()
{
	bAttached = false;
	ENQUEUE_RENDER_COMMAND(URemotePlayReflectionCaptureComponentInitialize)(
		[this](FRHICommandListImmediate& RHICmdList)
		{
			Initialize_RenderThread(RHICmdList);
		}
	);
}

void URemotePlayReflectionCaptureComponent::UpdateContents(FScene *Scene,UTextureRenderTargetCube *InSourceTexture, ERHIFeatureLevel::Type FeatureLevel)
{
	ENQUEUE_RENDER_COMMAND(RemotePlayCopyReflections)(
		[this, Scene, InSourceTexture, FeatureLevel](FRHICommandListImmediate& RHICmdList)
		{
			//SCOPED_DRAW_EVENT(RHICmdList, RemotePlayReflectionCaptureComponent);
			UpdateReflections_RenderThread(RHICmdList, Scene, InSourceTexture, FeatureLevel);
		}
	);
}

void URemotePlayReflectionCaptureComponent::PrepareFrame(FScene *Scene, FSurfaceTexture *TargetSurfaceTexture, ERHIFeatureLevel::Type FeatureLevel)
{
	ENQUEUE_RENDER_COMMAND(RemotePlayWriteReflectionsToSurface)(
		[this, Scene, TargetSurfaceTexture, FeatureLevel](FRHICommandListImmediate& RHICmdList)
		{
			//SCOPED_DRAW_EVENT(RHICmdList, RemotePlayReflectionCaptureComponent);
			WriteReflections_RenderThread(RHICmdList, Scene, TargetSurfaceTexture, FeatureLevel);
		}
	);
}

void URemotePlayReflectionCaptureComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	TArray<AActor*> DirLights;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ADirectionalLight::StaticClass(), DirLights);

	TResourceArray<FShaderDirectionalLight> ShaderDirLights;

	for (auto DirLight : DirLights)
	{
		auto C = DirLight->GetComponentByClass(UDirectionalLightComponent::StaticClass());
		if (!C)
			continue;

		auto LC = Cast<UDirectionalLightComponent>(C);

		FShaderDirectionalLight Light;
		Light.Color = LC->LightColor;
		Light.Direction = LC->GetDirection();
		Light.intensity = LC->Intensity;

		ShaderDirLights.Add(Light);
	}

	ShaderDirLightsQueue.Enqueue(MoveTemp(ShaderDirLights));
}

template<typename ShaderType>
void URemotePlayReflectionCaptureComponent::DispatchUpdateReflectionsShader(FRHICommandListImmediate& RHICmdList, FTextureRHIRef SourceTextureRHI, FUnorderedAccessViewRHIRef Target_UAV, ERHIFeatureLevel::Type FeatureLevel)
{
	if (!SceneProxy->EncodedHDRCubemap)
		return;
	int32 OriginalSize = SceneProxy->EncodedHDRCubemap->TextureRHI->GetSizeXYZ().X;
	const uint32 NumThreadGroupsX = OriginalSize / ShaderType::kThreadGroupSize;
	const uint32 NumThreadGroupsY = OriginalSize / ShaderType::kThreadGroupSize;

	TShaderMap<FGlobalShaderType>* GlobalShaderMap = GetGlobalShaderMap(FeatureLevel);

	TShaderMapRef<ShaderType> ComputeShader(GlobalShaderMap);
	ComputeShader->SetParameters(RHICmdList, SourceTextureRHI,SceneProxy->EncodedHDRCubemap->TextureRHI, Target_UA,0V);
	SetComputePipelineState(RHICmdList, GETSAFERHISHADER_COMPUTE(*ComputeShader));
	DispatchComputeShader(RHICmdList, *ComputeShader, NumThreadGroupsX, NumThreadGroupsY, 1);
	ComputeShader->UnsetParameters(RHICmdList);
}
