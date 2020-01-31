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
		RandomSeed.Bind(Initializer.ParameterMap, TEXT("RandomSeed"));
	}
	FUpdateReflectionsBaseCS() = default;
	void SetInputs(
		FRHICommandList& RHICmdList,
		FTextureCubeRHIRef InputCubeMapTextureRef,
		FShaderResourceViewRHIRef DirLightsShaderResourceViewRef = FShaderResourceViewRHIRef(nullptr, false),
		int32 InDirLightCount = 0)
	{
		FComputeShaderRHIParamRef ShaderRHI = GetComputeShader();

		if (InputCubeMapTextureRef)
		{
			SetTextureParameter(RHICmdList, ShaderRHI, InputCubeMap, DefaultSampler, TStaticSamplerState<SF_Bilinear>::GetRHI(), InputCubeMapTextureRef);
			//check(InputCubeMap.IsBound());
		}
		SetShaderValue(RHICmdList, ShaderRHI, SourceSize, InputCubeMapTextureRef->GetSize());

		if (DirLightsShaderResourceViewRef)
		{
			SetShaderValue(RHICmdList, ShaderRHI, DirLightCount, InDirLightCount);
			SetSRVParameter(RHICmdList, ShaderRHI, DirLightStructBuffer, DirLightsShaderResourceViewRef);
		}
	}
	void SetInputs(
		FRHICommandList& RHICmdList,
		FShaderResourceViewRHIRef InputCubeMapSRVRef,
		int32 InSourceSize,
		FShaderResourceViewRHIRef DirLightsShaderResourceViewRef=FShaderResourceViewRHIRef(nullptr,false),
		int32 InDirLightCount=0)
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
		SetShaderValue(RHICmdList, ShaderRHI, SourceSize, InSourceSize);

		if (DirLightsShaderResourceViewRef)
		{
			SetShaderValue(RHICmdList, ShaderRHI, DirLightCount, InDirLightCount);
			SetSRVParameter(RHICmdList, ShaderRHI, DirLightStructBuffer, DirLightsShaderResourceViewRef);
		}
	}
	 
	void SetOutputs(
		FRHICommandList& RHICmdList,
		FTextureCubeRHIRef OutputColorTextureRef,
		FUnorderedAccessViewRHIRef OutputColorTextureUAVRef,
		int32 InTargetSize)
	{
		FComputeShaderRHIParamRef ShaderRHI = GetComputeShader();
		//SetTextureParameter(RHICmdList, ShaderRHI, InputCubeMap, DefaultSampler, TStaticSamplerState<SF_Bilinear>::GetRHI(), InputCubeMapTextureRef);
		RWOutputTexture.SetTexture(RHICmdList, ShaderRHI, OutputColorTextureRef, OutputColorTextureUAVRef);
		SetShaderValue(RHICmdList, ShaderRHI, TargetSize, InTargetSize);
	}


	void SetParameters(
		FRHICommandList& RHICmdList,
		FIntPoint InOffset,
		float InRoughness,
		uint32 InRandomSeed)
	{
		FComputeShaderRHIParamRef ShaderRHI = GetComputeShader();
		SetShaderValue(RHICmdList, ShaderRHI, Offset, InOffset);
		SetShaderValue(RHICmdList, ShaderRHI, Roughness, InRoughness);
		SetShaderValue(RHICmdList, ShaderRHI, RandomSeed, InRandomSeed);
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
		Ar << RandomSeed;
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
	FShaderParameter RandomSeed;
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
	
	specularOffset	=FIntPoint(0, 0);
	diffuseOffset	= specularOffset + FIntPoint(specularSize*3/2, specularSize*2);
	roughOffset	 = FIntPoint(3 * specularSize, 0);
	lightOffset = diffuseOffset + FIntPoint(specularSize * 3 / 2, specularSize * 2);
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
	const uint32_t NumMips = t.TextureCubeRHIRef->GetNumMips();
	for (uint32_t i = 0; i < NumMips; i++)
	{
		if (t.UnorderedAccessViewRHIRefs[i])
			t.UnorderedAccessViewRHIRefs[i]->Release();
		if (t.TextureCubeMipRHIRefs[i])
			t.TextureCubeMipRHIRefs[i]->Release();
	}
	if (t.TextureCubeRHIRef)
		t.TextureCubeRHIRef->Release();
}

void URemotePlayReflectionCaptureComponent::Initialize_RenderThread(FRHICommandListImmediate& RHICmdList)
{
	Init(RHICmdList, SpecularCubeTexture, specularSize, 3);
	Init(RHICmdList, RoughSpecularCubeTexture, specularSize, 3);
	Init(RHICmdList,DiffuseCubeTexture,diffuseSize, 1);
	Init(RHICmdList,LightingCubeTexture,lightSize, 1);
}

void URemotePlayReflectionCaptureComponent::Release_RenderThread(FRHICommandListImmediate& RHICmdList)
{
	Release(SpecularCubeTexture);
	Release(RoughSpecularCubeTexture);
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

	const int32 SourceSize = SourceCubeResource->GetSizeX();


	int32 CaptureIndex = 0;
	FTextureRHIRef TargetResource;
	FIntPoint Offset0(0, 0);
	if (OverrideTexture)
	{
		TargetResource = OverrideTexture->GetRenderTargetResource()->TextureRHI;
	}
	else
	{
		CaptureIndex = FindOrAllocateCubemapIndex(Scene, this);
		if (CaptureIndex>=0&&Scene&&Scene->ReflectionSceneData.CubemapArray.GetCubemapSize())
		{
			FSceneRenderTargetItem &rt = Scene->ReflectionSceneData.CubemapArray.GetRenderTarget();
			if (rt.IsValid())
				TargetResource = rt.TargetableTexture;
		}
	}
	randomSeed++;
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
			const int32 MipSize = specularSize;
			CopyCubemapShader->SetInputs(RHICmdList,SourceCubeResource->GetTextureRHI());
			CopyCubemapShader->SetOutputs(RHICmdList,
				SpecularCubeTexture.TextureCubeRHIRef,
				SpecularCubeTexture.UnorderedAccessViewRHIRefs[0],MipSize
				);
			CopyCubemapShader->SetParameters(RHICmdList, Offset0, 0.f, randomSeed);
			SetComputePipelineState(RHICmdList, GETSAFERHISHADER_COMPUTE(*CopyCubemapShader));
			uint32 NumThreadGroupsXY = (specularSize +1) / ShaderType::kThreadGroupSize;
			DispatchComputeShader(RHICmdList, *CopyCubemapShader, NumThreadGroupsXY, NumThreadGroupsXY, CubeFace_MAX);
			CopyCubemapShader->UnsetParameters(RHICmdList);
		}
		// The other mips are generated
		int32 MipSize = specularSize;
		int32 PrevMipSize = specularSize;
		uint32_t NumMips = SpecularCubeTexture.TextureCubeRHIRef->GetNumMips();
		for (uint32 MipIndex = 1; MipIndex < NumMips; MipIndex++)
		{
			float roughness = RoughnessFromMip((float)MipIndex, (float)(2* NumMips));
			FUpdateReflectionsBaseCS *Shader=static_cast<FUpdateReflectionsBaseCS *>((roughness < 0.99f) ? s:r);
			PrevMipSize = MipSize;
			MipSize = (MipSize + 1) / 2;
			// Apparently Unreal can't cope with copying from one mip to another in the same texture. So we use the original cube:
			Shader->SetInputs(RHICmdList,
				SourceCubeResource->GetTextureRHI());
			Shader->SetOutputs(RHICmdList,
				SpecularCubeTexture.TextureCubeRHIRef,
				SpecularCubeTexture.UnorderedAccessViewRHIRefs[MipIndex],MipSize
			);
			Shader->SetParameters(RHICmdList, Offset0, roughness, randomSeed);
			SetComputePipelineState(RHICmdList, GETSAFERHISHADER_COMPUTE(Shader));
			uint32 NumThreadGroupsXY = MipSize > ShaderType::kThreadGroupSize ? MipSize / ShaderType::kThreadGroupSize : 1;
			DispatchComputeShader(RHICmdList, Shader, NumThreadGroupsXY, NumThreadGroupsXY, CubeFace_MAX);
			Shader->UnsetParameters(RHICmdList);
		}
		MipSize = specularSize;
		PrevMipSize = specularSize;
		for (uint32 MipIndex = 0; MipIndex < RoughSpecularCubeTexture.TextureCubeRHIRef->GetNumMips(); MipIndex++)
		{
			float roughness = RoughnessFromMip(float(NumMips+MipIndex), (float)(2 * NumMips));
			FUpdateReflectionsBaseCS *Shader = static_cast<FUpdateReflectionsBaseCS *>((roughness < 0.99f) ? s : r);
			Shader->SetInputs(RHICmdList,SourceCubeResource->GetTextureRHI());
			Shader->SetOutputs(RHICmdList,RoughSpecularCubeTexture.TextureCubeRHIRef, RoughSpecularCubeTexture.UnorderedAccessViewRHIRefs[MipIndex], MipSize);
			Shader->SetParameters(RHICmdList, Offset0, roughness, randomSeed);
			SetComputePipelineState(RHICmdList, GETSAFERHISHADER_COMPUTE(Shader));
			uint32 NumThreadGroupsXY = MipSize > ShaderType::kThreadGroupSize ? MipSize / ShaderType::kThreadGroupSize : 1;
			DispatchComputeShader(RHICmdList, Shader, NumThreadGroupsXY, NumThreadGroupsXY, CubeFace_MAX);
			Shader->UnsetParameters(RHICmdList);
			PrevMipSize = MipSize;
			MipSize = (MipSize + 1) / 2;
		}
	}

	// Diffuse Reflections
	{
		typedef FUpdateReflectionsCS<EUpdateReflectionsVariant::UpdateDiffuse> ShaderType;
		TShaderMapRef<FUpdateReflectionsCS<EUpdateReflectionsVariant::UpdateDiffuse>> ComputeShader(ShaderMap);
		FUpdateReflectionsBaseCS *Shader = static_cast<FUpdateReflectionsBaseCS *>(*ComputeShader);
		uint32_t NumMips = DiffuseCubeTexture.TextureCubeRHIRef->GetNumMips();
		int32 MipSize = DiffuseCubeTexture.TextureCubeRHIRef->GetSize();
		for (uint32 MipIndex = 0; MipIndex < NumMips; MipIndex++)
		{
			Shader->SetInputs(RHICmdList, SourceCubeResource->GetTextureRHI());
			Shader->SetOutputs(RHICmdList, DiffuseCubeTexture.TextureCubeRHIRef, DiffuseCubeTexture.UnorderedAccessViewRHIRefs[MipIndex], MipSize);
			Shader->SetParameters(RHICmdList, Offset0, 1.0, randomSeed);
			SetComputePipelineState(RHICmdList, GETSAFERHISHADER_COMPUTE(Shader));
			uint32 NumThreadGroupsXY = MipSize > ShaderType::kThreadGroupSize ? MipSize / ShaderType::kThreadGroupSize : 1;
			DispatchComputeShader(RHICmdList, Shader, NumThreadGroupsXY, NumThreadGroupsXY, CubeFace_MAX);
			ComputeShader->UnsetParameters(RHICmdList);
			MipSize = (MipSize + 1) / 2;
		}
		if (TargetResource)
		{
			for (uint32 MipIndex = 0; MipIndex < std::min(NumMips, (uint32)TargetResource->GetNumMips()); MipIndex++)
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
			TShaderMapRef<FUpdateReflectionsCS<EUpdateReflectionsVariant::UpdateLighting>> ComputeShader(ShaderMap);
			FUpdateReflectionsBaseCS *Shader = static_cast<FUpdateReflectionsBaseCS *>(*ComputeShader);
			int32 MipSize = lightSize;
			uint32_t NumMips = LightingCubeTexture.TextureCubeRHIRef->GetNumMips();
			for (uint32 MipIndex = 0; MipIndex < NumMips; MipIndex++)
			{
				int32 PrevMipSize = MipSize;
				MipSize = (MipSize + 1) / 2;
				float roughness = RoughnessFromMip((float)MipIndex, (float)NumMips);
				Shader->SetInputs(RHICmdList, SourceCubeResource->GetTextureRHI(), DirLightSRV, ShaderDirLights.Num());
				Shader->SetOutputs(RHICmdList, LightingCubeTexture.TextureCubeRHIRef, LightingCubeTexture.UnorderedAccessViewRHIRefs[MipIndex], MipSize);
				Shader->SetParameters(RHICmdList, Offset0, 1.0, randomSeed);
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
	const int32 NumMips = CubeTexture.TextureCubeRHIRef->GetNumMips();
	SCOPED_DRAW_EVENT(RHICmdList, WriteReflections);

	int32 MipSize = EffectiveTopMipSize;
	// 2 * W for the colour cube two face height 
	for (int32 MipIndex = 0; MipIndex < NumMips; MipIndex++)
	{
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
		MipSize = (MipSize + 1) / 2;
	}
}
// write the reflections to the UAV of the output video stream.
void URemotePlayReflectionCaptureComponent::WriteReflections_RenderThread(FRHICommandListImmediate& RHICmdList, FScene *Scene, FSurfaceTexture *TargetSurfaceTexture, ERHIFeatureLevel::Type FeatureLevel
	,FIntPoint StartOffset)
{
	SCOPED_DRAW_EVENT(RHICmdList, WriteReflections);

	typedef FUpdateReflectionsCS<EUpdateReflectionsVariant::WriteToStream> ShaderType;

	auto* ShaderMap = GetGlobalShaderMap(FeatureLevel);

	TShaderMapRef<FUpdateReflectionsCS<EUpdateReflectionsVariant::WriteToStream>> ComputeShader(ShaderMap);
	SCOPED_DRAW_EVENT(RHICmdList, WriteReflections);

	FShader *Shader = ComputeShader.operator*();
	Decompose_RenderThread(RHICmdList, DiffuseCubeTexture, TargetSurfaceTexture, Shader, StartOffset+diffuseOffset);
	Decompose_RenderThread(RHICmdList, SpecularCubeTexture, TargetSurfaceTexture, Shader, StartOffset + specularOffset);
	Decompose_RenderThread(RHICmdList, RoughSpecularCubeTexture, TargetSurfaceTexture, Shader, StartOffset+roughOffset);
	Decompose_RenderThread(RHICmdList, LightingCubeTexture, TargetSurfaceTexture, Shader, StartOffset + lightOffset);
}

void URemotePlayReflectionCaptureComponent::Initialise()
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

void URemotePlayReflectionCaptureComponent::PrepareFrame(FScene *Scene, FSurfaceTexture *TargetSurfaceTexture, ERHIFeatureLevel::Type FeatureLevel, FIntPoint StartOffset)
{
	ENQUEUE_RENDER_COMMAND(RemotePlayWriteReflectionsToSurface)(
		[this, Scene, TargetSurfaceTexture, FeatureLevel, StartOffset](FRHICommandListImmediate& RHICmdList)
		{
			//SCOPED_DRAW_EVENT(RHICmdList, RemotePlayReflectionCaptureComponent);
			WriteReflections_RenderThread(RHICmdList, Scene, TargetSurfaceTexture, FeatureLevel, StartOffset);
		}
	);
}

void URemotePlayReflectionCaptureComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
}