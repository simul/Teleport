// Copyright 2018 Simul.co
#define WIN32_LEAN_AND_MEAN

#include "Components/CaptureComponent.h"
#include "RemotePlayModule.h"
#include "Pipelines/EncodePipelineMonoscopic.h"
#include "Pipelines/NetworkPipeline.h"

#include "Engine.h"
#include "Engine/GameViewportClient.h"

#include "GameFramework/Actor.h"
#include "RemotePlaySettings.h"
#include "RemotePlayMonitor.h"
#include "RemotePlayReflectionCaptureComponent.h"
#include "ContentStreaming.h"

URemotePlayCaptureComponent::URemotePlayCaptureComponent()
	: bRenderOwner(false)
	, RemotePlayContext(nullptr)
	, RemotePlayReflectionCaptureComponent(nullptr)
	, bIsStreaming(false)
	, bSendKeyframe(false)
{
	PrimaryComponentTick.bCanEverTick = true;
	bCaptureEveryFrame = true;
	bCaptureOnMovement = false; 
}

URemotePlayCaptureComponent::~URemotePlayCaptureComponent()
{
}

void URemotePlayCaptureComponent::BeginPlay()
{	
	ShowFlags.EnableAdvancedFeatures();
	ShowFlags.SetTemporalAA(false);
	ShowFlags.SetAntiAliasing(true);

	if (TextureTarget && !TextureTarget->bCanCreateUAV)
	{
		TextureTarget->bCanCreateUAV = true;
	}

	ARemotePlayMonitor* Monitor = ARemotePlayMonitor::Instantiate(GetWorld());
	if (Monitor->bOverrideTextureTarget && Monitor->SceneCaptureTextureTarget)
	{
		TextureTarget = Monitor->SceneCaptureTextureTarget;
	}

	// Make sure that there is enough time in the render queue.
	UKismetSystemLibrary::ExecuteConsoleCommand(GetWorld(), FString("g.TimeoutForBlockOnRenderFence 300000"));

	Super::BeginPlay();	

	AActor* OwnerActor = GetTypedOuter<AActor>();
	if (bRenderOwner)
	{
		RemotePlayReflectionCaptureComponent = Cast<URemotePlayReflectionCaptureComponent>(OwnerActor->GetComponentByClass(URemotePlayReflectionCaptureComponent::StaticClass()));
	}
	else
	{
		check(OwnerActor);

		TArray<AActor*> OwnerAttachedActors;
		OwnerActor->GetAttachedActors(OwnerAttachedActors);
		HiddenActors.Add(OwnerActor);
		HiddenActors.Append(OwnerAttachedActors);

	}
}

void URemotePlayCaptureComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	Super::EndPlay(Reason);
}

void URemotePlayCaptureComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Aidan: Below allows the capture to avail of Unreal's texture streaming
	// Add the view information every tick because its only used for one tick and then
	// removed by the streaming manager.
	int32 W = TextureTarget->GetSurfaceWidth();
	float FOV = 90.0f;
	IStreamingManager::Get().AddViewInformation(GetComponentLocation(), W, W / FMath::Tan(FOV));

	ARemotePlayMonitor *Monitor = ARemotePlayMonitor::Instantiate(GetWorld());
	if (bCaptureEveryFrame && Monitor && Monitor->bDisableMainCamera)
	{
		CaptureScene();
	}
}

const FRemotePlayEncodeParameters &URemotePlayCaptureComponent::GetEncodeParams()
{
	if (EncodeParams.bDecomposeCube)
	{
		int32 W = TextureTarget->GetSurfaceWidth();
		// 3 across...
		EncodeParams.FrameWidth = 3 * W;
		// and 2 down... for the colour, depth, and light cubes.
		EncodeParams.FrameHeight = 3 * W; // (W + W / 2);
	}
	else
	{
		EncodeParams.FrameWidth = 2048;
		EncodeParams.FrameHeight = 1024 + 512;
	}
	return EncodeParams;
}

void URemotePlayCaptureComponent::UpdateSceneCaptureContents(FSceneInterface* Scene)
{

	ARemotePlayMonitor *Monitor = ARemotePlayMonitor::Instantiate(GetWorld());
	if (Monitor&&Monitor->VideoEncodeFrequency > 1)
	{
		static int u = 1;
		u--;
		u = std::min(Monitor->VideoEncodeFrequency, u);
		if (!u)
		{
			u = Monitor->VideoEncodeFrequency;
		}
		else
		{
			return;
		}
	}
	// Aidan: The parent function belongs to SceneCaptureComponentCube and is located in SceneCaptureComponent.cpp. 
	// The parent function calls UpdateSceneCaptureContents function in SceneCaptureRendering.cpp.
	// UpdateSceneCaptureContents enqueues the rendering commands to render to the scene capture cube's render target.
	// The parent function is called from the static function UpdateDeferredCaptures located in
	// SceneCaptureComponent.cpp. UpdateDeferredCaptures is called by the BeginRenderingViewFamily function in SceneRendering.cpp.
	// Therefore the rendering commands queued after this function call below directly follow the scene capture cube's commands in the queue.

	Super::UpdateSceneCaptureContents(Scene);

	if (TextureTarget&&RemotePlayContext)
	{
		if (!RemotePlayContext->EncodePipeline.IsValid())
		{
			RemotePlayContext->EncodePipeline.Reset(new FEncodePipelineMonoscopic);
			RemotePlayContext->EncodePipeline->Initialize(EncodeParams, RemotePlayContext, Monitor, RemotePlayContext->ColorQueue.Get(), RemotePlayContext->DepthQueue.Get());

			if (RemotePlayReflectionCaptureComponent)
			{
				RemotePlayReflectionCaptureComponent->Initialize();
				RemotePlayReflectionCaptureComponent->bAttached = true;
			}
		}
		FTransform Transform = GetComponentTransform();
	
		if (Monitor->bDoCubemapCulling)
		{
			TArray<bool> FaceIntersectionResults;
			TArray<bool> QuadIntersectionResults;
			CullHiddenCubeSegments(FaceIntersectionResults, QuadIntersectionResults);
		}

		RemotePlayContext->EncodePipeline->PrepareFrame(Scene, TextureTarget, Transform);
		if (RemotePlayReflectionCaptureComponent && EncodeParams.bDecomposeCube)
		{
			RemotePlayReflectionCaptureComponent->UpdateContents(
				Scene->GetRenderScene(),
				TextureTarget,
				Scene->GetFeatureLevel());
			int32 W = TextureTarget->GetSurfaceWidth();
			FIntPoint Offset0((W*3)/2,W*2);
			RemotePlayReflectionCaptureComponent->PrepareFrame(
				Scene->GetRenderScene(),
				RemotePlayContext->EncodePipeline->GetSurfaceTexture(),
				Scene->GetFeatureLevel(),Offset0);
		}
		RemotePlayContext->EncodePipeline->EncodeFrame(Scene, TextureTarget, Transform, bSendKeyframe);
		// The client must request it again if it needs it
		bSendKeyframe = false;
	}
}

void URemotePlayCaptureComponent::CullHiddenCubeSegments(TArray<bool>& FaceIntersectionResults, TArray<bool>& QuadIntersectionResults)
{
	// Aidan: Currently not going to do this on GPU because doing it on game thread allows us to  
	// share the output with the capture component to cull faces from rendering.
	ARemotePlayMonitor *Monitor = ARemotePlayMonitor::Instantiate(GetWorld());
	assert(Monitor->BlocksPerCubeFace % 2 == 0);

	float W = TextureTarget->GetSurfaceWidth();

	//const FVector Forward = ClientCamInfo.Orientation.GetForwardVector();
	//const FVector Up = ClientCamInfo.Orientation.GetUpVector();
	FLookAtMatrix ViewMatrix = FLookAtMatrix(FVector::ZeroVector, ClientCamInfo.Orientation.GetForwardVector(), ClientCamInfo.Orientation.GetUpVector());

	float FOV = FMath::DegreesToRadians(ClientCamInfo.FOV);

	FMatrix ProjectionMatrix;
	if (static_cast<int32>(ERHIZBuffer::IsInverted) == 1)
	{
		ProjectionMatrix = FReversedZPerspectiveMatrix(FOV, (float)ClientCamInfo.Width, (float)ClientCamInfo.Height, GNearClippingPlane, W / 2);
	}
	else
	{
		ProjectionMatrix = FPerspectiveMatrix(FOV, (float)ClientCamInfo.Width, (float)ClientCamInfo.Height, GNearClippingPlane, W / 2);
	}

	FMatrix VP = ViewMatrix * ProjectionMatrix;

	// Use to prevent shared vectors from being tested more than once
	TMap<FVector, bool> VectorIntersectionMap;

	float QuadSize = (W / (float)Monitor->BlocksPerCubeFace) * 2;

	float halfWidth = W / 2;
	// Unreal Engine coordinates: X is forward, Y is side, Z is up, 
	const FVector OrigStartPos = { halfWidth, -halfWidth, -halfWidth }; // Bottom left of front face
	const FVector OrigRightDir = { 0, 1, 0 }; // Going right
	const FVector OrigUpDir = { 0, 0, 1 }; // Going up

	const uint32 HalfQuadsPerFace = Monitor->BlocksPerCubeFace / 2;

	TArray<FVector> VArray;

	// Quaternions for rotating front face to get the points of the other faces.
	FQuat FaceQuats[6] = 
	{
		{0, 0, 0, 1}, // Front face (identity as no rotation needed)
		{0, 0, 1, 180}, // Back face
		{0, 0, 1, 90}, // Right face
		{0, 0, 1, -90}, // Left face
		{0, 1, 0, -90}, // Top face
		{0, 1, 0, 90}  // Bottom face
	};
	
	// Iterate through all six faces
	for (uint32 i = 0; i < 6; ++i)
	{
		const FQuat& q = FaceQuats[i];
		const FVector Axis = { q.X, q.Y, q.Z };
		FVector Pos = OrigStartPos.RotateAngleAxis(q.W, Axis);
		const FVector RightVec = OrigRightDir.RotateAngleAxis(q.W, Axis) * QuadSize;
		const FVector UpVec = OrigUpDir.RotateAngleAxis(q.W, Axis) * QuadSize;

		bool FaceIntersects = false;

		// Go right
		for (uint32 j = 0; j < HalfQuadsPerFace; ++j)
		{
			FVector QuadPos = Pos;
			// Go up
			for (uint32 k = 0; k < HalfQuadsPerFace; ++k)
			{
				FVector TopLeft = QuadPos + UpVec;
				// Bottom left, top left, bottom right, top right
				FVector Points[4] = { QuadPos, TopLeft, QuadPos + RightVec, TopLeft + RightVec };

				for (auto& V : Points)
				{
					VArray.Add(V);
				}
				bool Intersects = false;
				for(const auto& V : Points)
				{
					bool* Value = VectorIntersectionMap.Find(V);
					if (Value != nullptr && *Value)
					{		
						Intersects = true;
						break;		
					}
					else
					{
						if (VectorIntersectsFrustum(V, VP))
						{
							Intersects = true;
							VectorIntersectionMap.Add(TPair<FVector, bool>(V, true));
							break;
						}
						else
						{
							VectorIntersectionMap.Add(TPair<FVector, bool>(V, false));
						}
					}
				}	
				
				// Quad Index = (i * Monitor->BlocksPerCubeFace) + (j * HalfQuadsPerFace) + k;
				QuadIntersectionResults.Add(Intersects);

				if (Intersects)
				{
					FaceIntersects = true;
				}

				QuadPos = TopLeft;
			}
			Pos += RightVec;
		}
		FaceIntersectionResults.Add(FaceIntersects);
	}
}

bool URemotePlayCaptureComponent::VectorIntersectsFrustum(const FVector& Vector, const FMatrix& ViewProjection)
{
	FVector4 ProjPos = ViewProjection.TransformVector(Vector);
	ProjPos.X /= ProjPos.W;
	ProjPos.Y /= ProjPos.W;
	ProjPos.Z /= ProjPos.W;

	if (ProjPos.X >= -1 && ProjPos.X <= 1 && ProjPos.Y >= -1 && ProjPos.Y <= 1)
	{
		return true;
	}
	return false;
}

void URemotePlayCaptureComponent::StartStreaming(FRemotePlayContext *Context)
{
	RemotePlayContext = Context;

	ARemotePlayMonitor *Monitor = ARemotePlayMonitor::Instantiate(GetWorld());
	if (!ViewportDrawnDelegateHandle.IsValid())
	{
		if (UGameViewportClient* GameViewport = GEngine->GameViewport)
		{
			ViewportDrawnDelegateHandle = GameViewport->OnDrawn().AddUObject(this, &URemotePlayCaptureComponent::OnViewportDrawn);
			GameViewport->bDisableWorldRendering = Monitor->bDisableMainCamera;
		}
	}

	bIsStreaming = true;
	bCaptureEveryFrame = true;
	bSendKeyframe = false;
}

void URemotePlayCaptureComponent::StopStreaming()
{
	if (!RemotePlayContext)
	{
		UE_LOG(LogRemotePlay, Warning, TEXT("Capture: null RemotePlayContext"));
		return;
	}

	if (ViewportDrawnDelegateHandle.IsValid())
	{
		if (UGameViewportClient* GameViewport = GEngine->GameViewport)
		{
			GameViewport->OnDrawn().Remove(ViewportDrawnDelegateHandle);
		}
		ViewportDrawnDelegateHandle.Reset();
	}

	RemotePlayContext = nullptr;

	bCaptureEveryFrame = false;
	UE_LOG(LogRemotePlay, Log, TEXT("Capture: Stopped streaming"));
}

void URemotePlayCaptureComponent::RequestKeyframe()
{
	bSendKeyframe = true;
}

void URemotePlayCaptureComponent::OnViewportDrawn()
{
}

FCameraInfo& URemotePlayCaptureComponent::GetClientCameraInfo()
{
	return ClientCamInfo;
}

