// (c) 2018 Simul.co

#include "RemotePlayViewComponent.h"
#include "RemotePlayPlugin.h"
#include "RemotePlayRHI.h"

#include "GameFramework/Actor.h"

#include "Engine.h"
#include "Engine/NetConnection.h"
#include "Engine/World.h"

#include "Public/GlobalShader.h"
#include "Public/PipelineStateCache.h"
#include "Public/ShaderParameters.h"
#include "Public/ShaderParameterUtils.h"
#include "Public/RHIUtilities.h"
#include "Public/ScreenRendering.h"

#include "LibStreaming.hpp"

DECLARE_FLOAT_COUNTER_STAT(TEXT("RemotePlayView"), Stat_GPU_RemotePlayView, STATGROUP_GPU);

class FNV12toRGBA_CS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FNV12toRGBA_CS, Global);
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

	FNV12toRGBA_CS() {}
	FNV12toRGBA_CS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		InputBuffer.Bind(Initializer.ParameterMap, TEXT("InputBuffer"));
		OutputTexture.Bind(Initializer.ParameterMap, TEXT("OutputTexture"));
		Pitch.Bind(Initializer.ParameterMap, TEXT("Pitch"));
	}

	void SetParameters(
		FRHICommandList& RHICmdList,
		FShaderResourceViewRHIRef InputBufferRef,
		FTexture2DRHIRef OutputTextureRef,
		FUnorderedAccessViewRHIRef OutputTextureUAVRef,
		uint32 InPitch
		)
	{
		FComputeShaderRHIParamRef ShaderRHI = GetComputeShader();
		RHICmdList.SetShaderResourceViewParameter(ShaderRHI, 0, InputBufferRef);
		OutputTexture.SetTexture(RHICmdList, ShaderRHI, OutputTextureRef, OutputTextureUAVRef);
		SetShaderValue(RHICmdList, ShaderRHI, Pitch, InPitch);
	}

	void UnsetParameters(FRHICommandList& RHICmdList)
	{
		FComputeShaderRHIParamRef ShaderRHI = GetComputeShader();
		OutputTexture.UnsetUAV(RHICmdList, ShaderRHI);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << InputBuffer;
		Ar << OutputTexture;
		Ar << Pitch;
		return bShaderHasOutdatedParameters;
	}
	
	static const uint32 kThreadGroupSize = 16;

private:
	FShaderResourceParameter InputBuffer;
	FRWShaderParameter OutputTexture;
	FShaderParameter Pitch;
};

class FResolveFrameVS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FResolveFrameVS, Global);
public:
	static bool ShouldCache(EShaderPlatform Platform)
	{
		return IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM4);
	}

	FResolveFrameVS() {}
	FResolveFrameVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{}
};

class FResolveFramePS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FResolveFramePS, Global)
public:
	static bool ShouldCache(EShaderPlatform Platform)
	{
		return IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM4);
	}

	FResolveFramePS() {}
	FResolveFramePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		InTexture.Bind(Initializer.ParameterMap, TEXT("InTexture"));
		InSampler.Bind(Initializer.ParameterMap, TEXT("InSampler"));
	}

	void SetParameters(FRHICommandList& RHICmdList, FSamplerStateRHIParamRef SamplerStateRef, FTexture2DRHIParamRef TextureRef)
	{
		FPixelShaderRHIParamRef ShaderRHI = GetPixelShader();
		SetTextureParameter(RHICmdList, ShaderRHI, InTexture, InSampler, SamplerStateRef, TextureRef);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << InTexture;
		Ar << InSampler;
		return bShaderHasOutdatedParameters;
	}

private:
	FShaderResourceParameter InTexture;
	FShaderResourceParameter InSampler;
};

IMPLEMENT_SHADER_TYPE(, FNV12toRGBA_CS,  TEXT("/Plugin/RemotePlayPlugin/Private/NV12toRGBA.usf"),   TEXT("MainCS"), SF_Compute)
IMPLEMENT_SHADER_TYPE(, FResolveFrameVS, TEXT("/Plugin/RemotePlayPlugin/Private/ResolveFrame.usf"), TEXT("MainVS"), SF_Vertex)
IMPLEMENT_SHADER_TYPE(, FResolveFramePS, TEXT("/Plugin/RemotePlayPlugin/Private/ResolveFrame.usf"), TEXT("MainPS"), SF_Pixel)

class FViewContext
{
public:
	FViewContext(const FRemotePlayStreamParameters& InParams)
		: StreamParams(InParams)
	{}

	void Initialize_GameThread(const FString& InAddr)
	{
		StreamingIO.Reset(Streaming::createNetworkIO(Streaming::NetworkAPI::ENET));
		RemoteAddr = InAddr;
	}

	bool EnsureConnected_GameThread()
	{
		if(!bConnected)
		{
			try {
				StreamingIO->connect(TCHAR_TO_UTF8(*RemoteAddr), (int)StreamParams.ConnectionPort);
				bConnected = true;
			}
			catch(const std::exception&) {
				return false;
			}
		}
		return true;
	}

	void __declspec(noinline) Initialize_RenderThread(FRHICommandListImmediate& RHICmdList)
	{
		RHI.Reset(new FRemotePlayRHI(RHICmdList, StreamParams.FrameWidth, StreamParams.FrameHeight));
		RHI->createSurface(Streaming::SurfaceFormat::ARGB);

		StreamingDecoder.Reset(Streaming::createDecoder(Streaming::Platform::NV));

		StreamingDecoder->initialize(RHI.Get(), StreamParams.FrameWidth, StreamParams.FrameHeight);
	}

	void __declspec(noinline) Release_RenderThread(FRHICommandListImmediate& RHICmdList)
	{
		StreamingDecoder->shutdown();
	}

	bool DecodeFrame()
	{
		if(StreamingIO->processClient())
		{
			Streaming::Bitstream Bitstream = StreamingIO->read();
			if(Bitstream)
			{
				StreamingDecoder->decode(Bitstream);
				return true;
			}
		}
		return false;
	}

	void ProcessFrame(
		FRHICommandListImmediate& RHICmdList,
		ERHIFeatureLevel::Type FeatureLevel)
	{
		SCOPED_DRAW_EVENT(RHICmdList, RemotePlayView);
		
		TShaderMap<FGlobalShaderType>* GlobalShaderMap = GetGlobalShaderMap(FeatureLevel);
		TShaderMapRef<FNV12toRGBA_CS> ComputeShader(GlobalShaderMap);

		const uint32 NumThreadGroupsX = StreamParams.FrameWidth / FNV12toRGBA_CS::kThreadGroupSize;
		const uint32 NumThreadGroupsY = StreamParams.FrameHeight / FNV12toRGBA_CS::kThreadGroupSize;
		ComputeShader->SetParameters(RHICmdList, RHI->VideoBufferSRV, RHI->SurfaceRHI, RHI->SurfaceUAV, RHI->VideoBufferPitch);
		SetComputePipelineState(RHICmdList, GETSAFERHISHADER_COMPUTE(*ComputeShader));
		DispatchComputeShader(RHICmdList, *ComputeShader, NumThreadGroupsX, NumThreadGroupsY, 1);
		ComputeShader->UnsetParameters(RHICmdList);
	}

	void ResolveFrameToRenderTarget(
		FRHICommandListImmediate& RHICmdList,
		FTextureRenderTargetResource* RenderTargetResource,
		ERHIFeatureLevel::Type FeatureLevel)
	{
		SCOPED_DRAW_EVENT(RHICmdList, RemotePlayView);
		
		SetRenderTarget(
			RHICmdList,
			RenderTargetResource->GetRenderTargetTexture(),
			FTextureRHIRef(),
			ESimpleRenderTargetMode::EUninitializedColorAndDepth,
			FExclusiveDepthStencil::DepthNop_StencilNop
		);

		const FIntPoint ViewportSize = RenderTargetResource->GetSizeXY();
		RHICmdList.SetViewport(0, 0, 0.0f, ViewportSize.X, ViewportSize.Y, 1.0f);

		TShaderMap<FGlobalShaderType>* GlobalShaderMap = GetGlobalShaderMap(FeatureLevel);
		TShaderMapRef<FResolveFrameVS> VertexShader(GlobalShaderMap);
		TShaderMapRef<FResolveFramePS> PixelShader(GlobalShaderMap);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.PrimitiveType = PT_TriangleStrip;
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GScreenVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		PixelShader->SetParameters(RHICmdList, TStaticSamplerState<SF_Point>::GetRHI(), RHI->SurfaceRHI);

		static const FScreenVertex ScreenQuadVertices[] =
		{
			{FVector2D{-1.0f,  1.0f}, FVector2D{0.0f, 0.0f}},
			{FVector2D{ 1.0f,  1.0f}, FVector2D{1.0f, 0.0f}},
			{FVector2D{-1.0f, -1.0f}, FVector2D{0.0f, 1.0f}},
			{FVector2D{ 1.0f, -1.0f}, FVector2D{1.0f, 1.0f}},
		};
		DrawPrimitiveUP(RHICmdList, PT_TriangleStrip, 2, ScreenQuadVertices, sizeof(ScreenQuadVertices[0]));
	}

	TUniquePtr<FRemotePlayRHI> RHI;

	TUniquePtr<Streaming::DecoderInterface> StreamingDecoder;
	TUniquePtr<Streaming::NetworkIOInterface> StreamingIO;

	FString RemoteAddr;
	bool bConnected = false;

	const FRemotePlayStreamParameters StreamParams;
};
	
void URemotePlayViewComponent::BeginInitializeContext(class FViewContext* InContext, const FString& InAddr) const
{
	InContext->Initialize_GameThread(InAddr);
	ENQUEUE_RENDER_COMMAND(RemotePlayInitializeViewContextCommand)(
		[InContext](FRHICommandListImmediate& RHICmdList)
		{
			InContext->Initialize_RenderThread(RHICmdList);
		}
	);
}
	
void URemotePlayViewComponent::BeginReleaseContext(class FViewContext* InContext) const
{
	ENQUEUE_RENDER_COMMAND(RemotePlayReleaseViewContextCommand)(
		[InContext](FRHICommandListImmediate& RHICmdList)
		{
			InContext->Release_RenderThread(RHICmdList);
			delete InContext;
		}
	);
}
	
void URemotePlayViewComponent::BeginProcessFrame(class FViewContext* InContext) const
{
	FTextureRenderTargetResource* RenderTargetResource = TextureTarget->GameThread_GetRenderTargetResource();
	const ERHIFeatureLevel::Type FeatureLevel = GetWorld()->Scene->GetFeatureLevel();

	ENQUEUE_RENDER_COMMAND(RemotePlayViewCommand)(
		[InContext, RenderTargetResource, FeatureLevel](FRHICommandListImmediate& RHICmdList)
		{
			if(InContext->DecodeFrame())
			{
				InContext->ProcessFrame(RHICmdList, FeatureLevel);
				InContext->ResolveFrameToRenderTarget(RHICmdList, RenderTargetResource, FeatureLevel);
			}
		}
	);
}

URemotePlayViewComponent::URemotePlayViewComponent()
	: Context(nullptr)
{
	PrimaryComponentTick.bCanEverTick = true;
	bWantsInitializeComponent = true;

	StreamParams.FrameWidth  = 2048;
	StreamParams.FrameHeight = 1024;
	StreamParams.ConnectionPort = 31337;
}

void URemotePlayViewComponent::UninitializeComponent()
{
	if(Context)
	{
		BeginReleaseContext(Context);
		Context = nullptr;
	}
	Super::UninitializeComponent();
}

void URemotePlayViewComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
			
	AActor* OuterActor = GetTypedOuter<AActor>();
	check(OuterActor);

	if(Context && TextureTarget)
	{
		if(Context->EnsureConnected_GameThread())
		{
			BeginProcessFrame(Context);
		}
	}
	if(DisplayActor)
	{
		DisplayActor->SetActorLocation(OuterActor->GetActorLocation());
	}
}
	
void URemotePlayViewComponent::StartStreamingViewFromServer()
{
	if(Context || GetWorld()->IsServer())
	{
		return;
	}
	if(!DisplayActorClass)
	{
		UE_LOG(LogRemotePlayPlugin, Warning, TEXT("No display actor configured"));
		return;
	}

	AActor* OuterActor = GetTypedOuter<AActor>();
	check(OuterActor);

	if(UNetConnection* NetConnection = OuterActor->GetNetConnection())
	{
		const FString RemoteAddr = NetConnection->LowLevelGetRemoteAddress();

		Context = new FViewContext(StreamParams);
		BeginInitializeContext(Context, RemoteAddr);

		if(!DisplayActor)
		{
			FActorSpawnParameters SpawnParams;
			SpawnParams.Owner = OuterActor;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			DisplayActor = GetWorld()->SpawnActor<AActor>(DisplayActorClass, SpawnParams);
		}
	}
}
