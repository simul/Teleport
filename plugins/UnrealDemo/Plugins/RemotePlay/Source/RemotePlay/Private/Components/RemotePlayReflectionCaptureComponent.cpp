// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	
=============================================================================*/

#include "RemotePlayReflectionCaptureComponent.h"
#include "Engine/TextureRenderTargetCube.h"
#include "Renderer/Private/ScenePrivate.h"
#include "SceneManagement.h"
#include "RemotePlayRHI.h"
#include "PixelShaderUtils.h"

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
		RWOutputTexture.Bind(Initializer.ParameterMap, TEXT("RWOutputTexture"));
	}

	void SetParameters(
		FRHICommandList& RHICmdList,
		FTextureCubeRHIRef InputCubeMapTextureRef,
		FTextureCubeRHIRef OutputColorTextureRef,
		FUnorderedAccessViewRHIRef OutputColorTextureUAVRef)
	{
		FComputeShaderRHIParamRef ShaderRHI = GetComputeShader();

		SetTextureParameter(RHICmdList, ShaderRHI, InputCubeMap, DefaultSampler, TStaticSamplerState<SF_Bilinear>::GetRHI(), InputCubeMapTextureRef);
		RWOutputTexture.SetTexture(RHICmdList, ShaderRHI, OutputColorTextureRef, OutputColorTextureUAVRef);

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
		return bShaderHasOutdatedParameters;
	}

	static const uint32 kThreadGroupSize = 16;

private:
	FShaderResourceParameter InputCubeMap;
	FShaderResourceParameter DefaultSampler;
	FRWShaderParameter RWOutputTexture;
};
/** Pixel shader used for filtering a mip. */
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

IMPLEMENT_SHADER_TYPE(, FUpdateReflectionsCS<EUpdateReflectionsVariant::FromOriginal>, TEXT("/Plugin/RemotePlay/Private/UpdateReflections.usf"), TEXT("MainCS"), SF_Compute)
IMPLEMENT_SHADER_TYPE(, FUpdateReflectionsPS, TEXT("/Plugin/RemotePlay/Private/UpdateReflections.usf"), TEXT("UpdateReflectionsPS"), SF_Pixel);

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
	BoxTransitionDistance = 100;
	Mobility = EComponentMobility::Movable;
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
	ReflectionCubeTexture.TextureCubeRHIRef = RHICmdList.CreateTextureCube(128, PF_FloatRGBA,NumMips, TexCreate_UAV,CreateInfo);
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
	FTextureRenderTargetCubeResource* SourceCubeResource = static_cast<FTextureRenderTargetCubeResource*>(InSourceTexture->GetRenderTargetResource());
	const int32 EffectiveTopMipSize = InSourceTexture->SizeX;

	const int32 NumMips =  FMath::CeilLogTwo(EffectiveTopMipSize) + 1;

	const int32 CaptureIndex = FindOrAllocateCubemapIndex(Scene, this);
	FTextureRHIParamRef CubemapArray;
	auto &rt=Scene->ReflectionSceneData.CubemapArray.GetRenderTarget();
	if (Scene->ReflectionSceneData.CubemapArray.IsValid() &&
		Scene->ReflectionSceneData.CubemapArray.GetRenderTarget().IsValid())
	{
		CubemapArray = Scene->ReflectionSceneData.CubemapArray.GetRenderTarget().ShaderResourceTexture;
	}
	else
	{
		return;
	}
	//DispatchUpdateReflectionsShader<FUpdateReflectionsCS<EUpdateReflectionsVariant::FromOriginal>>(
	//	RHICmdList, InSourceTexture->TextureReference.TextureReferenceRHI.GetReference()->GetTextureReference(), Target_UAV,FeatureLevel);

	FSceneRenderTargetItem& DestCube = Scene->ReflectionSceneData.CubemapArray.GetRenderTarget();


	SCOPED_DRAW_EVENT(RHICmdList, UpdateReflections);

	auto* ShaderMap = GetGlobalShaderMap(FeatureLevel);

	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
	GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();

	// Downsample all the mips, each one reads from the mip above it
	for (int32 MipIndex = 0; MipIndex < NumMips; MipIndex++)
	{
		const int32 MipSize = 1 << (NumMips - MipIndex - 1);
		SCOPED_DRAW_EVENT(RHICmdList, RemotePlay);
		for (int32 CubeFace = 0; CubeFace < CubeFace_MAX; CubeFace++)
		{
#if 1
			TShaderMapRef<FUpdateReflectionsCS<EUpdateReflectionsVariant::FromOriginal>> ComputeShader(ShaderMap);
			//RHICmdList.CopyToResolveTarget(InSourceTexture->Resource->TextureRHI, DestCube.ShaderResourceTexture, FResolveParams(FResolveRect(), (ECubeFace)CubeFace, MipIndex, 0, CaptureIndex));

			ComputeShader->SetParameters(RHICmdList, SourceCubeResource->GetTextureRHI(),
				ReflectionCubeTexture.TextureCubeRHIRef,
				ReflectionCubeTexture.UnorderedAccessViewRHIRefs[MipIndex]);
			SetComputePipelineState(RHICmdList, GETSAFERHISHADER_COMPUTE(*ComputeShader));
			const uint32 NumThreadGroupsX = 128 / 16;
			const uint32 NumThreadGroupsY = 128 / 16;
			DispatchComputeShader(RHICmdList, *ComputeShader, NumThreadGroupsX, NumThreadGroupsY, 1);
			ComputeShader->UnsetParameters(RHICmdList);


			// Now copy this face of this cube into the cubemap array.

			FResolveParams ResolveParams;
			ResolveParams.Rect = FResolveRect();
			ResolveParams.SourceArrayIndex = 0;
			ResolveParams.DestArrayIndex = CaptureIndex;
			ResolveParams.CubeFace = (ECubeFace)CubeFace;
			ResolveParams.MipIndex = MipIndex;
			RHICmdList.CopyToResolveTarget(ReflectionCubeTexture.TextureCubeRHIRef
				, rt.ShaderResourceTexture, ResolveParams);
#else
			FRHIRenderPassInfo RPInfo(ReflectionCubeTexture.TextureCubeRHIRef, ERenderTargetActions::DontLoad_Store, nullptr, MipIndex, CubeFace);
			RPInfo.bGeneratingMips = true;
			RHICmdList.BeginRenderPass(RPInfo, TEXT("CreateCubeMips"));
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

			const FIntRect ViewRect(0, 0, MipSize, MipSize);
			RHICmdList.SetViewport(0, 0, 0.0f, MipSize, MipSize, 1.0f);


			TShaderMapRef<FScreenVS> VertexShader(ShaderMap);
			TShaderMapRef<FUpdateReflectionsPS> PixelShader(ShaderMap);

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			{
				const FPixelShaderRHIParamRef ShaderRHI = PixelShader->GetPixelShader();

				SetShaderValue(RHICmdList, ShaderRHI, PixelShader->CubeFace, CubeFace);
				SetShaderValue(RHICmdList, ShaderRHI, PixelShader->MipIndex, MipIndex);

				SetShaderValue(RHICmdList, ShaderRHI, PixelShader->NumMips, NumMips);
				FShaderResourceViewRHIRef TextureParameterSRV = RHICreateShaderResourceView(SourceCubeResource->GetTextureRHI()->GetTextureCube(), 0);

				SetSRVParameter(RHICmdList, ShaderRHI, PixelShader->SourceTexture
					, TextureParameterSRV);
				SetSamplerParameter(RHICmdList, ShaderRHI, PixelShader->SourceTextureSampler, TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
			}
			
			float ClipSpaceQuadZ = 0.0f;
			FPixelShaderUtils::DrawFullscreenQuad(RHICmdList, 1);
			
			RHICmdList.EndRenderPass();
#endif
		}
	}

	RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, &CubemapArray, 1);
	
}

void URemotePlayReflectionCaptureComponent::Initialize()
{
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
	ComputeShader->SetParameters(RHICmdList, SourceTextureRHI,SceneProxy->EncodedHDRCubemap->TextureRHI, Target_UAV);
	SetComputePipelineState(RHICmdList, GETSAFERHISHADER_COMPUTE(*ComputeShader));
	DispatchComputeShader(RHICmdList, *ComputeShader, NumThreadGroupsX, NumThreadGroupsY, 1);
	ComputeShader->UnsetParameters(RHICmdList);
}
