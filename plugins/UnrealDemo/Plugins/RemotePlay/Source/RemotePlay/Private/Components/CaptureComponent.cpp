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
	if (bIsStreaming && bCaptureEveryFrame && Monitor && Monitor->bDisableMainCamera)
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
	// Do render to scene capture or do encoding if not streaming
	if (!bIsStreaming)
		return;

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

	TArray<bool> QuadsToRender;
	if (Monitor->bDoCubemapCulling)
	{
		FacesToRender.Reset();
		CullHiddenCubeSegments(FacesToRender, QuadsToRender);
	}
	else
	{
		QuadsToRender.Init(true, 6);
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

		/*for (auto& v : QuadsToRender)
		{
			v = true;
		}
		QuadsToRender[Monitor->Index] = false;*/
		RemotePlayContext->EncodePipeline->PrepareFrame(Scene, TextureTarget, Transform, QuadsToRender);
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

bool URemotePlayCaptureComponent::ShouldRenderFace(int32 FaceId) const
{
	return FacesToRender[FaceId];
}

void URemotePlayCaptureComponent::CullHiddenCubeSegments(TArray<bool>& FaceIntersectionResults, TArray<bool>& QuadIntersectionResults)
{
	// Aidan: Currently not going to do this on GPU because doing it on game thread allows us to  
	// share the output with the capture component to cull faces from rendering.
	ARemotePlayMonitor *Monitor = ARemotePlayMonitor::Instantiate(GetWorld());

	const FVector Forward = ClientCamInfo.Orientation.GetForwardVector() * 500;
	const FVector Up = ClientCamInfo.Orientation.GetUpVector();
	const FLookAtMatrix ViewMatrix = FLookAtMatrix(FVector::ZeroVector, Forward, Up);

	// Convert FOV from degrees to radians 
	const float FOV = FMath::DegreesToRadians(ClientCamInfo.FOV);

	const float CubeWidth = TextureTarget->GetSurfaceWidth();
	const float HalfWidth = CubeWidth / 2;
	const float QuadSize = (CubeWidth / Monitor->BlocksPerCubeFaceAcross);

	FMatrix ProjectionMatrix;
	if (static_cast<int32>(ERHIZBuffer::IsInverted) == 1)
	{
		ProjectionMatrix = AdjustProjectionMatrixForRHI(FReversedZPerspectiveMatrix(FOV, (float)ClientCamInfo.Width, (float)ClientCamInfo.Height, 0, 0));
	}
	else
	{
		ProjectionMatrix = AdjustProjectionMatrixForRHI(FPerspectiveMatrix(FOV, (float)ClientCamInfo.Width, (float)ClientCamInfo.Height, 0, 0));
	}

	const FMatrix VP = ViewMatrix * ProjectionMatrix;

	// Use to prevent shared vectors from being tested more than once
	TMap<FVector, bool> VectorIntersectionMap;

	// Unreal Engine coordinates: X is forward, Y is right, Z is up, 
	const FVector StartPos = FVector(HalfWidth, -HalfWidth, -HalfWidth); // Bottom left of front face

	TArray<FVector> VArray;

	// First Quat is to get position, forward and side vectors relative to front face
	// Second qauternion is rotate to match Unreal's cubemap face rotations
	static const FQuat FrontQuat = FQuat(1, 0, 0, -90); // No need to multiply as first qauternion is identity
	static const FQuat BackQuat = FQuat(0, 0, 1, 180) * FQuat(1, 0, 0, 90);
	static const FQuat RightQuat = FQuat(0, 0, 1, 90) * FQuat(0, 1, 0, 180);
	static const FQuat LeftQuat = FQuat(1, 0, 0, -90); // No need to multiply as second quaternion is identity
	static const FQuat TopQuat = FQuat(0, 1, 0, -90) * FQuat(0, 0, 1, -90);
	static const FQuat BottomQuat = FQuat(0, 1, 0, 90) * FQuat(0, 0, 1, 90);

	FQuat FaceQuats[6] = { FrontQuat, BackQuat, RightQuat, LeftQuat, TopQuat, BottomQuat };

	// Iterate through all six faces
	for (uint32 i = 0; i < 6; ++i)
	{
		const FQuat& q = FaceQuats[i];
		const FVector Axis = { q.X, q.Y, q.Z };
		const FVector RightVec = FVector::RightVector.RotateAngleAxis(q.W, Axis) * QuadSize;
		const FVector UpVec = FVector::UpVector.RotateAngleAxis(q.W, Axis) * QuadSize;
		FVector Pos = StartPos.RotateAngleAxis(q.W, Axis);

		bool FaceIntersects = false;

		// Go right
		for (int32 j = 0; j < Monitor->BlocksPerCubeFaceAcross; ++j)
		{
			FVector QuadPos = Pos;
			// Go up
			for (int32 k = 0; k < Monitor->BlocksPerCubeFaceAcross; ++k)
			{
				FVector TopLeft = QuadPos + UpVec;
				// Bottom left, top left, bottom right, top right
				FVector Points[4] = { QuadPos, TopLeft, QuadPos + RightVec, TopLeft + RightVec };

				// VArray is just for debugging
				for (auto& V : Points)
				{
					VArray.Add(V);
				}
				bool Intersects = false;
				for(const auto& V : Points)
				{
					bool* Value = VectorIntersectionMap.Find(V);
					if (Value && *Value)
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
						VectorIntersectionMap.Add(TPair<FVector, bool>(V, false));	
					}
				}	

				int32 QuadIndex = (i * Monitor->BlocksPerCubeFaceAcross * Monitor->BlocksPerCubeFaceAcross) + (j * Monitor->BlocksPerCubeFaceAcross) + k;
				if (QuadIndex == Monitor->CullQuadIndex)
				{
					Intersects = false;
				}
				
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
	FPlane Result = ViewProjection.TransformFVector4(FVector4(Vector, 1.0f));
	if (Result.W <= 0.0f)
	{
		return false;
	}
	// the result of this will be x and y coords in -1..1 projection space
	const float RHW = 1.0f / Result.W;
	Result.X *= RHW;
	Result.Y *= RHW; 

	if (Result.X < -1.0f || Result.X > 1.0f || Result.Y < -1.0f || Result.Y > 1.0f)
	{
		return false;
	}
	return true;
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

	ClientCamInfo.Orientation = GetComponentTransform().GetRotation();

	FacesToRender.Init(true, 6);
}

void URemotePlayCaptureComponent::StopStreaming()
{
	bIsStreaming = false;
	bCaptureEveryFrame = false;
	FacesToRender.Empty();

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

