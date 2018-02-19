// (c) 2018 Simul.co

#include "RemotePlayCaptureComponent.h"
#include "RemotePlayPlugin.h"

#include "Engine.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Engine/TextureRenderTargetCube.h"

#include "Public/GlobalShader.h"
#include "Public/PipelineStateCache.h"
#include "Public/ShaderParameters.h"
#include "Public/ShaderParameterUtils.h"

DECLARE_FLOAT_COUNTER_STAT(TEXT("RemotePlayCapture"), Stat_GPU_RemotePlayCapture, STATGROUP_GPU);

class FCaptureProjectionCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FCaptureProjectionCS, Global);
public:
	static bool ShouldCache(EShaderPlatform Platform)
	{
		return IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM5);
	}
	static void ModifyCompilationEnvironment(EShaderPlatform Platform, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Platform, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), kThreadGroupSize);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEY"), kThreadGroupSize);
	}

	FCaptureProjectionCS() {}
	FCaptureProjectionCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		CaptureCubeMap.Bind(Initializer.ParameterMap, TEXT("CaptureCubeMap"));
		CaptureSampler.Bind(Initializer.ParameterMap, TEXT("CaptureSampler"));
		OutputTexture.Bind(Initializer.ParameterMap, TEXT("OutputTexture"));
	}

	void SetParameters(
		FRHICommandList& RHICmdList,
		FTextureRHIRef CaptureCubeMapTextureRef,
		FTexture2DRHIRef OutputTextureRef,
		FUnorderedAccessViewRHIRef OutputTextureUAVRef)
	{
		FComputeShaderRHIParamRef ShaderRHI = GetComputeShader();
		SetTextureParameter(RHICmdList, ShaderRHI, CaptureCubeMap, CaptureSampler, TStaticSamplerState<SF_Bilinear>::GetRHI(), CaptureCubeMapTextureRef);
		OutputTexture.SetTexture(RHICmdList, ShaderRHI, OutputTextureRef, OutputTextureUAVRef);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << CaptureCubeMap;
		Ar << CaptureSampler;
		Ar << OutputTexture;
		return bShaderHasOutdatedParameters;
	}
	
	static const uint32 kThreadGroupSize = 16;

private:
	FShaderResourceParameter CaptureCubeMap;
	FShaderResourceParameter CaptureSampler;
	FRWShaderParameter OutputTexture;
};

IMPLEMENT_SHADER_TYPE(, FCaptureProjectionCS, TEXT("/Plugin/RemotePlayPlugin/Private/CaptureProjection.usf"), TEXT("MainCS"), SF_Compute)

class FCaptureRenderContext
{
public:
	void Initialize_RenderThread(FRHICommandListImmediate& RHICmdList)
	{
		FRHIResourceCreateInfo CreateInfo;
		VideoSurfaceTextureRHI = RHICmdList.CreateTexture2D(kVideoSurfaceSize, kVideoSurfaceSize, EPixelFormat::PF_R8G8B8A8, 1, 1, TexCreate_UAV, CreateInfo);
		VideoSurfaceUAV = RHICmdList.CreateUnorderedAccessView(VideoSurfaceTextureRHI, 0);
	}

	void Release_RenderThread(FRHICommandListImmediate& RHICmdList)
	{
		VideoSurfaceUAV.SafeRelease();
		VideoSurfaceTextureRHI.SafeRelease();
	}

	FTexture2DRHIRef VideoSurfaceTextureRHI;
	FUnorderedAccessViewRHIRef VideoSurfaceUAV;
	static const uint32 kVideoSurfaceSize = 1024;
};
	
void URemotePlayCaptureComponent::BeginInitializeRenderContext(FCaptureRenderContext* InRenderContext)
{
	ENQUEUE_RENDER_COMMAND(RemotePlayInitializeCaptureRenderContextCommand)(
		[InRenderContext](FRHICommandListImmediate& RHICmdList)
		{
			InRenderContext->Initialize_RenderThread(RHICmdList);
		}
	);
}

void URemotePlayCaptureComponent::BeginReleaseRenderContext(FCaptureRenderContext* InRenderContext)
{
	ENQUEUE_RENDER_COMMAND(RemotePlayReleaseCaptureRenderContextCommand)(
		[InRenderContext](FRHICommandListImmediate& RHICmdList)
		{
			InRenderContext->Release_RenderThread(RHICmdList);
			delete InRenderContext;
		}
	);
}
	
void URemotePlayCaptureComponent::ProjectCapture_RenderThread(
	FRHICommandListImmediate& RHICmdList,
	FCaptureRenderContext* RenderContext,
	FTextureRenderTargetResource* RenderTargetResource,
	ERHIFeatureLevel::Type FeatureLevel)
{
	SCOPED_DRAW_EVENT(RHICmdList, RemotePlayCapture);

	TShaderMap<FGlobalShaderType>* GlobalShaderMap = GetGlobalShaderMap(FeatureLevel);
	TShaderMapRef<FCaptureProjectionCS> ComputeShader(GlobalShaderMap);

	const uint32 NumThreadGroups = FCaptureRenderContext::kVideoSurfaceSize / FCaptureProjectionCS::kThreadGroupSize;
	ComputeShader->SetParameters(RHICmdList, RenderTargetResource->TextureRHI, RenderContext->VideoSurfaceTextureRHI, RenderContext->VideoSurfaceUAV);

	SetComputePipelineState(RHICmdList, GETSAFERHISHADER_COMPUTE(*ComputeShader));
	DispatchComputeShader(RHICmdList, *ComputeShader, NumThreadGroups, NumThreadGroups, 1);
}	
	
URemotePlayCaptureComponent::URemotePlayCaptureComponent()
	: RenderContext(nullptr)
{
	bWantsInitializeComponent = true;
	bCaptureEveryFrame = true;
}
	
void URemotePlayCaptureComponent::UninitializeComponent()
{
	if(ViewportDrawnDelegateHandle.IsValid())
	{
		if(UGameViewportClient* GameViewport = GEngine->GameViewport)
		{
			GameViewport->OnDrawn().Remove(ViewportDrawnDelegateHandle);
		}
		ViewportDrawnDelegateHandle.Reset();
	}
	if(RenderContext)
	{
		BeginReleaseRenderContext(RenderContext);
		RenderContext = nullptr;
	}
	Super::UninitializeComponent();
}

void URemotePlayCaptureComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	UActorComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if(GetWorld()->IsServer() && bCaptureEveryFrame)
	{
		CaptureSceneDeferred();

		if(!ViewportDrawnDelegateHandle.IsValid())
		{
			if(UGameViewportClient* GameViewport = GEngine->GameViewport)
			{
				ViewportDrawnDelegateHandle = GameViewport->OnDrawn().AddUObject(this, &URemotePlayCaptureComponent::OnViewportDrawn);
			}
		}
	}
}

void URemotePlayCaptureComponent::OnViewportDrawn()
{
	if(GetWorld()->IsServer() && bCaptureEveryFrame)
	{
		ERHIFeatureLevel::Type FeatureLevel = GetWorld()->Scene->GetFeatureLevel();

		// TODO: Check for compute shader support.

		if(TextureTarget)
		{
			if(!RenderContext)
			{
				RenderContext = new FCaptureRenderContext;
				BeginInitializeRenderContext(RenderContext);
			}

			FTextureRenderTargetResource* RenderTargetResource = TextureTarget->GameThread_GetRenderTargetResource();
			FCaptureRenderContext* RenderContextCmd = RenderContext;

			ENQUEUE_RENDER_COMMAND(RemotePlayProjectCaptureCommand)(
				[RenderContextCmd, RenderTargetResource, FeatureLevel](FRHICommandListImmediate& RHICmdList)
				{
					ProjectCapture_RenderThread(RHICmdList, RenderContextCmd, RenderTargetResource, FeatureLevel);
				}
			);
		}
	}
}