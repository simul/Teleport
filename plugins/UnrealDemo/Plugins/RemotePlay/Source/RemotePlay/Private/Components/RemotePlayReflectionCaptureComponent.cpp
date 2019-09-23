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
	UpdateSpecular,
	UpdateDiffuse,
	UpdateLighting,
	Mip,
	MipRough,
	WriteToStream
};

class FUpdateReflectionsBaseCS : public FGlobalShader
{
public:
	static const uint32 kThreadGroupSize = 16;
	FUpdateReflectionsBaseCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		InputCubeMap.Bind(Initializer.ParameterMap, TEXT("InputCubeMap"));
	
		DefaultSampler.Bind(Initializer.ParameterMap, TEXT("DefaultSampler"));
		RWOutputTexture.Bind(Initializer.ParameterMap, TEXT("OutputTexture"));
		DirLightCount.Bind(Initializer.ParameterMap, TEXT("DirLightCount"));
		DirLightStructBuffer.Bind(Initializer.ParameterMap, TEXT("DirLights"));
		InputCubemapAsArrayTexture.Bind(Initializer.ParameterMap, TEXT("InputCubemapAsArrayTexture"));
		RWStreamOutputTexture.Bind(Initializer.ParameterMap, TEXT("StreamOutputTexture"));
		check(RWOutputTexture.IsUAVBound()|| RWStreamOutputTexture.IsUAVBound());
		Offset.Bind(Initializer.ParameterMap, TEXT("Offset"));
		SourceSize.Bind(Initializer.ParameterMap, TEXT("SourceSize"));
		TargetSize.Bind(Initializer.ParameterMap, TEXT("TargetSize"));
		Roughness.Bind(Initializer.ParameterMap, TEXT("Roughness"));
	}
	FUpdateReflectionsBaseCS() = default;
	void SetParameters(
		FRHICommandList& RHICmdList,
		FTextureCubeRHIRef InputCubeMapTextureRef,
		FTextureCubeRHIRef OutputColorTextureRef,
		FUnorderedAccessViewRHIRef OutputColorTextureUAVRef)
	{
		FComputeShaderRHIParamRef ShaderRHI = GetComputeShader();

		if(InputCubeMapTextureRef)
		{
			SetTextureParameter(RHICmdList, ShaderRHI, InputCubeMap, DefaultSampler, TStaticSamplerState<SF_Bilinear>::GetRHI(), InputCubeMapTextureRef);
			check(InputCubeMap.IsBound());
		}
		RWOutputTexture.SetTexture(RHICmdList, ShaderRHI, OutputColorTextureRef, OutputColorTextureUAVRef);
		SetShaderValue(RHICmdList, ShaderRHI, SourceSize, InputCubeMapTextureRef->GetSize());
		SetShaderValue(RHICmdList, ShaderRHI, TargetSize, OutputColorTextureRef->GetSize());
		SetShaderValue(RHICmdList, ShaderRHI, Roughness, 0.0f);
	}

	void SetParameters(
		FRHICommandList& RHICmdList,
		FShaderResourceViewRHIRef InputCubeMapSRVRef,
		FTextureCubeRHIRef OutputColorTextureRef,
		FUnorderedAccessViewRHIRef OutputColorTextureUAVRef,
		int32 InSourceSize,
		int32 InTargetSize,
		int32 InDirLightCount,
		float InRoughness,
		FShaderResourceViewRHIRef DirLightsShaderResourceViewRef)
	{
		FComputeShaderRHIParamRef ShaderRHI = GetComputeShader();

		if (InputCubeMapSRVRef)
		{
			SetSRVParameter(RHICmdList, ShaderRHI, InputCubeMap, InputCubeMapSRVRef);
			if (!InputCubeMap.IsBound())
			{
				check(InputCubeMap.IsBound());
			}
		}
			//SetTextureParameter(RHICmdList, ShaderRHI, InputCubeMap, DefaultSampler, TStaticSamplerState<SF_Bilinear>::GetRHI(), InputCubeMapTextureRef);
		RWOutputTexture.SetTexture(RHICmdList, ShaderRHI, OutputColorTextureRef, OutputColorTextureUAVRef);

		if (DirLightsShaderResourceViewRef)
		{
		SetShaderValue(RHICmdList, ShaderRHI, DirLightCount, InDirLightCount);
		SetSRVParameter(RHICmdList, ShaderRHI, DirLightStructBuffer, DirLightsShaderResourceViewRef);
	}
		SetShaderValue(RHICmdList, ShaderRHI, Offset, FIntPoint(0,0));
		SetShaderValue(RHICmdList, ShaderRHI, SourceSize, InSourceSize);
		SetShaderValue(RHICmdList, ShaderRHI, TargetSize, InTargetSize);
		SetShaderValue(RHICmdList, ShaderRHI, Roughness, InRoughness);
	}

	void UnsetParameters(FRHICommandList& RHICmdList)
	{
		FComputeShaderRHIParamRef ShaderRHI = GetComputeShader();
		RWOutputTexture.UnsetUAV(RHICmdList, ShaderRHI);
	}

	void SetStreamParameters(
		FRHICommandList& RHICmdList,
		FTextureCubeRHIRef InputColorTextureRef,
		FUnorderedAccessViewRHIRef InputColorTextureUAVRef,
		FTexture2DRHIRef OutputColorTextureRef,
		FUnorderedAccessViewRHIRef OutputColorTextureUAVRef,
		const FIntPoint& InOffset)
	{
		FComputeShaderRHIParamRef ShaderRHI = GetComputeShader();
		RWOutputTexture.SetTexture(RHICmdList, ShaderRHI, InputColorTextureRef, InputColorTextureUAVRef);
		RWStreamOutputTexture.SetTexture(RHICmdList, ShaderRHI, OutputColorTextureRef, OutputColorTextureUAVRef);
		SetShaderValue(RHICmdList, ShaderRHI, Offset, InOffset);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << InputCubeMap;
		Ar << DefaultSampler;
		Ar << RWOutputTexture;
		Ar << InputCubemapAsArrayTexture;
		Ar << RWStreamOutputTexture;
		Ar << DirLightCount;
		Ar << DirLightStructBuffer;
		Ar << Offset;
		Ar << SourceSize;
		Ar << TargetSize;
		Ar << Roughness;
		return bShaderHasOutdatedParameters;
	}
protected:
	FShaderResourceParameter InputCubeMap;
	FShaderResourceParameter DefaultSampler;
	FRWShaderParameter RWOutputTexture;
	FShaderResourceParameter InputCubemapAsArrayTexture;
	FRWShaderParameter RWStreamOutputTexture;
	FShaderParameter DirLightCount;
	FShaderResourceParameter DirLightStructBuffer;
	FShaderParameter Offset;
	FShaderParameter SourceSize;
	FShaderParameter TargetSize;
	FShaderParameter Roughness;
};

template<EUpdateReflectionsVariant Variant>
class FUpdateReflectionsCS : public FUpdateReflectionsBaseCS
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
		OutEnvironment.SetDefine(TEXT("MIP_ROUGH"), (Variant ==EUpdateReflectionsVariant::MipRough));
	}

	FUpdateReflectionsCS() = default;
	FUpdateReflectionsCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FUpdateReflectionsBaseCS(Initializer)
	{
	}

};


IMPLEMENT_SHADER_TYPE(, FUpdateReflectionsCS<EUpdateReflectionsVariant::NoSource>, TEXT("/Plugin/RemotePlay/Private/UpdateReflections.usf"), TEXT("NoSourceCS"), SF_Compute)
IMPLEMENT_SHADER_TYPE(, FUpdateReflectionsCS<EUpdateReflectionsVariant::UpdateSpecular>, TEXT("/Plugin/RemotePlay/Private/UpdateReflections.usf"), TEXT("UpdateSpecularCS"), SF_Compute)
IMPLEMENT_SHADER_TYPE(, FUpdateReflectionsCS<EUpdateReflectionsVariant::UpdateDiffuse>, TEXT("/Plugin/RemotePlay/Private/UpdateReflections.usf"), TEXT("UpdateDiffuseCS"), SF_Compute)
IMPLEMENT_SHADER_TYPE(, FUpdateReflectionsCS<EUpdateReflectionsVariant::UpdateLighting>, TEXT("/Plugin/RemotePlay/Private/UpdateReflections.usf"), TEXT("UpdateLightingCS"), SF_Compute)
IMPLEMENT_SHADER_TYPE(, FUpdateReflectionsCS<EUpdateReflectionsVariant::Mip>, TEXT("/Plugin/RemotePlay/Private/UpdateReflections.usf"), TEXT("FromMipCS"), SF_Compute)
IMPLEMENT_SHADER_TYPE(, FUpdateReflectionsCS<EUpdateReflectionsVariant::MipRough>, TEXT("/Plugin/RemotePlay/Private/UpdateReflections.usf"), TEXT("FromMipCS"), SF_Compute)
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

void URemotePlayReflectionCaptureComponent::Init(FRHICommandListImmediate& RHICmdList,FCubeTexture &t, int32 size, int32 NumMips)
{
	FRHIResourceCreateInfo CreateInfo;
	t.TextureCubeRHIRef = RHICmdList.CreateTextureCube(size, PF_FloatRGBA, NumMips, TexCreate_UAV, CreateInfo);
	for (int i = 0; i < NumMips; i++)
	{
		t.UnorderedAccessViewRHIRefs[i]= RHICmdList.CreateUnorderedAccessView(t.TextureCubeRHIRef, i);
	}
	for (int i = 0; i < NumMips; i++)
	{
		t.TextureCubeMipRHIRefs[i] = RHICmdList.CreateShaderResourceView(t.TextureCubeRHIRef, i);
}
}

void URemotePlayReflectionCaptureComponent::Release(FCubeTexture &t)
{
	if(SpecularCubeTexture.TextureCubeRHIRef)
		SpecularCubeTexture.TextureCubeRHIRef->Release();
	if (DiffuseCubeTexture.TextureCubeRHIRef)
		DiffuseCubeTexture.TextureCubeRHIRef->Release();
	if (LightingCubeTexture.TextureCubeRHIRef)
		LightingCubeTexture.TextureCubeRHIRef->Release();
	const int32 NumMips = FMath::CeilLogTwo(128) + 1;
	for (int i = 0; i < NumMips; i++)
	{
		if (SpecularCubeTexture.UnorderedAccessViewRHIRefs[i])
			SpecularCubeTexture.UnorderedAccessViewRHIRefs[i]->Release();
		if (DiffuseCubeTexture.UnorderedAccessViewRHIRefs[i])
			DiffuseCubeTexture.UnorderedAccessViewRHIRefs[i]->Release();
		if (LightingCubeTexture.UnorderedAccessViewRHIRefs[i])
			LightingCubeTexture.UnorderedAccessViewRHIRefs[i]->Release();
	}
}


void URemotePlayReflectionCaptureComponent::Initialize_RenderThread(FRHICommandListImmediate& RHICmdList)
{
	const int32 NumMips = FMath::CeilLogTwo(128) + 1;
	Init(RHICmdList, SpecularCubeTexture,128, NumMips);
	Init(RHICmdList,DiffuseCubeTexture,128, NumMips);
	Init(RHICmdList,LightingCubeTexture,128, NumMips);
}
void URemotePlayReflectionCaptureComponent::Release_RenderThread(FRHICommandListImmediate& RHICmdList)
{
	Release(SpecularCubeTexture);
	Release(DiffuseCubeTexture);
	Release(LightingCubeTexture);
}

static float RoughnessFromMip(float mip, float numMips)
{
	static float  roughness_mip_scale = 1.2f;
	return exp2((3.0f + mip - numMips) / roughness_mip_scale);
}


void URemotePlayReflectionCaptureComponent::UpdateReflections_RenderThread(
	FRHICommandListImmediate& RHICmdList, FScene *Scene,
	UTextureRenderTargetCube *InSourceTexture,
	ERHIFeatureLevel::Type FeatureLevel)
{
	if (!SpecularCubeTexture.TextureCubeRHIRef || !DiffuseCubeTexture.TextureCubeRHIRef || !LightingCubeTexture.TextureCubeRHIRef)
		Initialize_RenderThread(RHICmdList);
	FTextureRenderTargetCubeResource* SourceCubeResource = nullptr;
	if(InSourceTexture)
		SourceCubeResource =static_cast<FTextureRenderTargetCubeResource*>(InSourceTexture->GetRenderTargetResource());

	const int32 EffectiveTopMipSize = 128;

	const int32 NumMips =  FMath::CeilLogTwo(EffectiveTopMipSize) + 1;

	const int32 CaptureIndex = FindOrAllocateCubemapIndex(Scene, this);
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

	// Specular Reflections
	{
		typedef FUpdateReflectionsCS<EUpdateReflectionsVariant::UpdateSpecular> ShaderType;

		TShaderMapRef<FUpdateReflectionsCS<EUpdateReflectionsVariant::UpdateSpecular>> CopyCubemapShader(ShaderMap);
		TShaderMapRef<FUpdateReflectionsCS<EUpdateReflectionsVariant::Mip>> MipShader(ShaderMap);
		TShaderMapRef<FUpdateReflectionsCS<EUpdateReflectionsVariant::MipRough>> MipRoughShader(ShaderMap);
		FUpdateReflectionsBaseCS *s=(MipShader.operator*());
		FUpdateReflectionsBaseCS *r=(MipRoughShader.operator*());
		// The 0 mip is copied directly from the source cubemap,
		{
			const int32 MipSize = EffectiveTopMipSize;
			CopyCubemapShader->SetParameters(RHICmdList,
				SourceCubeResource->GetTextureRHI(),
				SpecularCubeTexture.TextureCubeRHIRef,
				SpecularCubeTexture.UnorderedAccessViewRHIRefs[0]
				);
			SetComputePipelineState(RHICmdList, GETSAFERHISHADER_COMPUTE(*CopyCubemapShader));
			uint32 NumThreadGroupsXY = (EffectiveTopMipSize+1) / ShaderType::kThreadGroupSize;
			DispatchComputeShader(RHICmdList, *CopyCubemapShader, NumThreadGroupsXY, NumThreadGroupsXY, CubeFace_MAX);
			CopyCubemapShader->UnsetParameters(RHICmdList);
		}
		// The other mips are generated
		int32 MipSize = EffectiveTopMipSize;
		int32 PrevMipSize = EffectiveTopMipSize;
		for (int32 MipIndex = 1; MipIndex < NumMips; MipIndex++)
		{
			float roughness = RoughnessFromMip((float)MipIndex, (float)NumMips);
			FUpdateReflectionsBaseCS *Shader=static_cast<FUpdateReflectionsBaseCS *>((roughness < 0.99f) ? s:r);
			PrevMipSize = MipSize;
			MipSize = (MipSize + 1) / 2;
			Shader->SetParameters(RHICmdList, SpecularCubeTexture.TextureCubeMipRHIRefs[MipIndex - 1],
				SpecularCubeTexture.TextureCubeRHIRef,
				SpecularCubeTexture.UnorderedAccessViewRHIRefs[MipIndex],PrevMipSize,MipSize,roughness,
				0,
				nullptr);
			// But apparently Unreal can't cope with copying from one mip to another in the same texture. So we use the original cube:
			Shader->SetParameters(RHICmdList,
				SourceCubeResource->GetTextureRHI(),
				SpecularCubeTexture.TextureCubeRHIRef,
				SpecularCubeTexture.UnorderedAccessViewRHIRefs[MipIndex]
			);
			SetComputePipelineState(RHICmdList, GETSAFERHISHADER_COMPUTE(Shader));
			uint32 NumThreadGroupsXY = MipSize > ShaderType::kThreadGroupSize ? MipSize / ShaderType::kThreadGroupSize : 1;
			DispatchComputeShader(RHICmdList, Shader, NumThreadGroupsXY, NumThreadGroupsXY, CubeFace_MAX);
			Shader->UnsetParameters(RHICmdList);
		}
	}

	// Diffuse Reflections
	{
		typedef FUpdateReflectionsCS<EUpdateReflectionsVariant::UpdateDiffuse> ShaderType;
		FGlobalShader *Shader = nullptr;
		TShaderMapRef<FUpdateReflectionsCS<EUpdateReflectionsVariant::UpdateDiffuse>> ComputeShader(ShaderMap);
		Shader = ComputeShader.operator*();
		for (int32 MipIndex = 0; MipIndex < NumMips; MipIndex++)
		{
			const int32 MipSize = 1 << (NumMips - MipIndex - 1);
			ComputeShader->SetParameters(RHICmdList, SourceCubeResource ? SourceCubeResource->GetTextureRHI() : nullptr,
				DiffuseCubeTexture.TextureCubeRHIRef,
				DiffuseCubeTexture.UnorderedAccessViewRHIRefs[MipIndex]
				);
			SetComputePipelineState(RHICmdList, GETSAFERHISHADER_COMPUTE(Shader));
			uint32 NumThreadGroupsXY = MipSize > ShaderType::kThreadGroupSize ? MipSize / ShaderType::kThreadGroupSize : 1;
			DispatchComputeShader(RHICmdList, Shader, NumThreadGroupsXY, NumThreadGroupsXY, CubeFace_MAX);
			ComputeShader->UnsetParameters(RHICmdList);
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
					RHICmdList.CopyToResolveTarget(SpecularCubeTexture.TextureCubeRHIRef
						, TargetResource, ResolveParams);
				}
			}
		}
	}

	// Lighting
	{
		TResourceArray<FShaderDirectionalLight> ShaderDirLights;

		for (auto& LightInfo : Scene->Lights)
		{
			if (LightInfo.LightType == LightType_Directional && LightInfo.LightSceneInfo->bVisible)
			{
				// We could update this later to only send dynamic lights if we want
				FShaderDirectionalLight ShaderDirLight;
				// The color includes the intensity. Divide by max intensity of 20
				ShaderDirLight.Color = LightInfo.Color * 0.05f; 
				ShaderDirLight.Direction = LightInfo.LightSceneInfo->Proxy->GetDirection();
				ShaderDirLights.Emplace(MoveTemp(ShaderDirLight));
			}
		}
		if (ShaderDirLights.Num())
		{
			FRHIResourceCreateInfo CreateInfo;
			CreateInfo.ResourceArray = &ShaderDirLights;

			FStructuredBufferRHIRef DirLightSB = RHICreateStructuredBuffer(
				sizeof(FShaderDirectionalLight),
				ShaderDirLights.Num() * sizeof(FShaderDirectionalLight),
				BUF_ShaderResource,
				CreateInfo
			);

			FShaderResourceViewRHIRef DirLightSRV = RHICreateShaderResourceView(DirLightSB);

				typedef FUpdateReflectionsCS<EUpdateReflectionsVariant::UpdateLighting> ShaderType;
			FGlobalShader *Shader = nullptr;
				TShaderMapRef<FUpdateReflectionsCS<EUpdateReflectionsVariant::UpdateLighting>> ComputeShader(ShaderMap);
			Shader = ComputeShader.operator*();
			int32 MipSize = EffectiveTopMipSize;
			for (int32 MipIndex = 0; MipIndex < NumMips; MipIndex++)
			{
				int32 PrevMipSize = MipSize;
				MipSize = (MipSize + 1) / 2;
				float roughness = RoughnessFromMip((float)MipIndex, (float)NumMips);
		
				ComputeShader->SetParameters(RHICmdList,  nullptr,
						LightingCubeTexture.TextureCubeRHIRef,
					LightingCubeTexture.UnorderedAccessViewRHIRefs[MipIndex], PrevMipSize, MipSize,roughness,
					ShaderDirLights.Num(),
					DirLightSRV);
				SetComputePipelineState(RHICmdList, GETSAFERHISHADER_COMPUTE(Shader));
				uint32 NumThreadGroupsXY = MipSize > ShaderType::kThreadGroupSize ? MipSize / ShaderType::kThreadGroupSize : 1;
				DispatchComputeShader(RHICmdList, Shader, NumThreadGroupsXY, NumThreadGroupsXY, CubeFace_MAX);
				ComputeShader->UnsetParameters(RHICmdList);
			}

			//Release the resources
			DirLightSRV->Release();
			DirLightSB->Release();
		}
	}
}

// write the reflections to the UAV of the output video stream.
void URemotePlayReflectionCaptureComponent::Decompose_RenderThread(FRHICommandListImmediate& RHICmdList
	, FCubeTexture &CubeTexture
	, FSurfaceTexture *TargetSurfaceTexture,  FShader *Shader, FIntPoint TargetOffset)
{
	auto* ComputeShader=(FUpdateReflectionsCS<EUpdateReflectionsVariant::WriteToStream> *)Shader;
	const int32 EffectiveTopMipSize = CubeTexture.TextureCubeRHIRef->GetSizeXYZ().X;
	const int32 NumMips = FMath::CeilLogTwo(EffectiveTopMipSize) + 1;
	SCOPED_DRAW_EVENT(RHICmdList, WriteReflections);

	// 2 * W for the colour cube two face height 
	for (int32 MipIndex = 0; MipIndex < NumMips; MipIndex++)
	{
		const int32 MipSize = 1 << (NumMips - MipIndex - 1);

		ComputeShader->SetStreamParameters(RHICmdList,
			CubeTexture.TextureCubeRHIRef,
			CubeTexture.UnorderedAccessViewRHIRefs[MipIndex],
			TargetSurfaceTexture->Texture,
			TargetSurfaceTexture->UAV,
			TargetOffset);
		SetComputePipelineState(RHICmdList, GETSAFERHISHADER_COMPUTE(Shader));
		uint32 NumThreadGroupsXY = MipSize > ComputeShader->kThreadGroupSize ? MipSize / ComputeShader->kThreadGroupSize : 1;
		DispatchComputeShader(RHICmdList, Shader, NumThreadGroupsXY, NumThreadGroupsXY, CubeFace_MAX);
		ComputeShader->UnsetParameters(RHICmdList);
		TargetOffset.Y += (MipSize * 2);
	}
}
// write the reflections to the UAV of the output video stream.
void URemotePlayReflectionCaptureComponent::WriteReflections_RenderThread(FRHICommandListImmediate& RHICmdList, FScene *Scene, FSurfaceTexture *TargetSurfaceTexture, ERHIFeatureLevel::Type FeatureLevel)
{
	SCOPED_DRAW_EVENT(RHICmdList, WriteReflections);

	typedef FUpdateReflectionsCS<EUpdateReflectionsVariant::WriteToStream> ShaderType;

	auto* ShaderMap = GetGlobalShaderMap(FeatureLevel);

	TShaderMapRef<FUpdateReflectionsCS<EUpdateReflectionsVariant::WriteToStream>> ComputeShader(ShaderMap);
	const int32 EffectiveTopMipSize	=128;
	const int32 NumMips				=FMath::CeilLogTwo(EffectiveTopMipSize) + 1;
	SCOPED_DRAW_EVENT(RHICmdList, WriteReflections);

	FShader *Shader = ComputeShader.operator*();
	int W = TargetSurfaceTexture->Texture->GetSizeX()/3;
	// Add EffectiveTopMipSize because we put to the right of specular cubemap
	uint32 xOffset = (W / 2) * 3;
	uint32 yOffset = W * 2;
	Decompose_RenderThread(RHICmdList, DiffuseCubeTexture, TargetSurfaceTexture, Shader, FIntPoint(xOffset, yOffset));
	xOffset += EffectiveTopMipSize*3;
	Decompose_RenderThread(RHICmdList, SpecularCubeTexture, TargetSurfaceTexture, Shader, FIntPoint(xOffset, yOffset));
	xOffset += EffectiveTopMipSize*3;
	Decompose_RenderThread(RHICmdList, LightingCubeTexture, TargetSurfaceTexture, Shader, FIntPoint(xOffset, yOffset));
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
}