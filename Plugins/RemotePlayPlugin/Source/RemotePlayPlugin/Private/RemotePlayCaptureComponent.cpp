// (c) 2018 Simul.co

#include "RemotePlayCaptureComponent.h"
#include "RemotePlayPlugin.h"
#include "RemotePlayRHI.h"

#include "Engine.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Engine/TextureRenderTargetCube.h"

#include "Public/GlobalShader.h"
#include "Public/PipelineStateCache.h"
#include "Public/ShaderParameters.h"
#include "Public/ShaderParameterUtils.h"

#include "LibStreaming.hpp"

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

	void UnsetParameters(FRHICommandList& RHICmdList)
	{
		FComputeShaderRHIParamRef ShaderRHI = GetComputeShader();
		OutputTexture.UnsetUAV(RHICmdList, ShaderRHI);
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

class FCaptureContext
{
public:
	FCaptureContext(const FRemotePlayStreamParameters& InParams)
		: StreamParams(InParams)
	{}

	void __declspec(noinline) Initialize_GameThread()
	{
		try
		{
			StreamingIO.Reset(Streaming::createNetworkIO(Streaming::NetworkAPI::ENET));
			StreamingIO->listen((int)StreamParams.ConnectionPort);
		}
		catch(const std::exception& e)
		{
			UE_LOG(LogRemotePlayPlugin, Fatal, TEXT("%s"), UTF8_TO_TCHAR(e.what()));
		}
	}

	void __declspec(noinline) Initialize_RenderThread(FRHICommandListImmediate& RHICmdList)
	{
		RHI.Reset(new FRemotePlayRHI(RHICmdList, StreamParams.FrameWidth, StreamParams.FrameHeight));

		try
		{
			StreamingEncoder.Reset(Streaming::createEncoder(Streaming::Platform::NV));
			StreamingEncoder->initialize(RHI.Get(), StreamParams.FrameWidth, StreamParams.FrameHeight, StreamParams.IDRFrequency);
		}
		catch(const std::exception& e)
		{
			UE_LOG(LogRemotePlayPlugin, Fatal, TEXT("%s"), UTF8_TO_TCHAR(e.what()));
		}

		FrameIndex = 0;
	}

	void __declspec(noinline) Release_RenderThread(FRHICommandListImmediate& RHICmdList)
	{
		try
		{
			StreamingEncoder->shutdown();
		}
		catch(const std::exception& e)
		{
			UE_LOG(LogRemotePlayPlugin, Fatal, TEXT("%s"), UTF8_TO_TCHAR(e.what()));
		}
	}

	void ProjectCapturedEnvironment(
		FRHICommandListImmediate& RHICmdList,
		FTextureRenderTargetResource* RenderTargetResource,
		ERHIFeatureLevel::Type FeatureLevel)
	{
		SCOPED_DRAW_EVENT(RHICmdList, RemotePlayCapture);

		TShaderMap<FGlobalShaderType>* GlobalShaderMap = GetGlobalShaderMap(FeatureLevel);
		TShaderMapRef<FCaptureProjectionCS> ComputeShader(GlobalShaderMap);

		const uint32 NumThreadGroupsX = StreamParams.FrameWidth / FCaptureProjectionCS::kThreadGroupSize;
		const uint32 NumThreadGroupsY = StreamParams.FrameHeight / FCaptureProjectionCS::kThreadGroupSize;
		ComputeShader->SetParameters(RHICmdList, RenderTargetResource->TextureRHI, RHI->SurfaceRHI, RHI->SurfaceUAV);
		SetComputePipelineState(RHICmdList, GETSAFERHISHADER_COMPUTE(*ComputeShader));
		DispatchComputeShader(RHICmdList, *ComputeShader, NumThreadGroupsX, NumThreadGroupsY, 1);
		ComputeShader->UnsetParameters(RHICmdList);
	}

	void ProcessCapturedEnvironment()
	{
		try
		{
			StreamingEncoder->encode(FrameIndex++);
			StreamingIO->processServer();

			const Streaming::Bitstream Bitstream = StreamingEncoder->lock();
			StreamingIO->write(Bitstream);
			StreamingEncoder->unlock();
		}
		catch(const std::exception& e)
		{
			UE_LOG(LogRemotePlayPlugin, Fatal, TEXT("%s"), UTF8_TO_TCHAR(e.what()));
		}
	}

	TUniquePtr<FRemotePlayRHI> RHI;

	TUniquePtr<Streaming::EncoderInterface> StreamingEncoder;
	TUniquePtr<Streaming::NetworkIOInterface> StreamingIO;
	uint64 FrameIndex;

	const FRemotePlayStreamParameters StreamParams;
};
	
void URemotePlayCaptureComponent::BeginInitializeContext(FCaptureContext* InContext) const
{
	InContext->Initialize_GameThread();
	ENQUEUE_RENDER_COMMAND(RemotePlayInitializeCaptureContextCommand)(
		[InContext](FRHICommandListImmediate& RHICmdList)
		{
			InContext->Initialize_RenderThread(RHICmdList);
		}
	);
}

void URemotePlayCaptureComponent::BeginReleaseContext(FCaptureContext* InContext) const
{
	ENQUEUE_RENDER_COMMAND(RemotePlayReleaseCaptureContextCommand)(
		[InContext](FRHICommandListImmediate& RHICmdList)
		{
			InContext->Release_RenderThread(RHICmdList);
			delete InContext;
		}
	);
}

void URemotePlayCaptureComponent::BeginCaptureFrame(FCaptureContext* InContext) const
{
	FTextureRenderTargetResource* RenderTargetResource = TextureTarget->GameThread_GetRenderTargetResource();
	const ERHIFeatureLevel::Type FeatureLevel = GetWorld()->Scene->GetFeatureLevel();

	ENQUEUE_RENDER_COMMAND(RemotePlayCaptureCommand)(
		[InContext, RenderTargetResource, FeatureLevel](FRHICommandListImmediate& RHICmdList)
		{
			InContext->ProjectCapturedEnvironment(RHICmdList, RenderTargetResource, FeatureLevel);
			InContext->ProcessCapturedEnvironment();
		}
	);
}
	
URemotePlayCaptureComponent::URemotePlayCaptureComponent()
	: Context(nullptr)
{
	bWantsInitializeComponent = true;
	bCaptureEveryFrame = true;

	StreamParams.FrameWidth  = 2048;
	StreamParams.FrameHeight = 1024;
	StreamParams.ConnectionPort = 31337;
	StreamParams.IDRFrequency = 60;
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
	if(Context)
	{
		BeginReleaseContext(Context);
		Context = nullptr;
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
		// TODO: Check for compute shader support.
		if(TextureTarget)
		{
			if(!Context)
			{
				Context = new FCaptureContext(StreamParams);
				BeginInitializeContext(Context);
			}
			BeginCaptureFrame(Context);
		}
	}
}
