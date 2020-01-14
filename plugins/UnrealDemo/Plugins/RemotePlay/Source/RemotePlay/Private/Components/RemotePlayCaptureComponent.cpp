// Copyright 2018 Simul.co
#define WIN32_LEAN_AND_MEAN

#include "RemotePlayCaptureComponent.h"

#include "ContentStreaming.h"
#include "Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/Actor.h"

#include "Pipelines/EncodePipelineMonoscopic.h"
#include "RemotePlayModule.h"
#include "RemotePlayMonitor.h"
#include "RemotePlayReflectionCaptureComponent.h"
#include "RemotePlaySettings.h"
#include "UnrealCasterContext.h"

URemotePlayCaptureComponent::URemotePlayCaptureComponent()
	: bRenderOwner(false)
	, UnrealCasterContext(nullptr)
	, RemotePlayReflectionCaptureComponent(nullptr)
	, bIsStreaming(false)
	, bSendKeyframe(false)
{
	PrimaryComponentTick.bCanEverTick = true;
	bCaptureEveryFrame = true;
	bCaptureOnMovement = false; 
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

const FUnrealCasterEncoderSettings& URemotePlayCaptureComponent::GetEncoderSettings()
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
	// Do not render to scene capture or do encoding if not streaming
	if(!bIsStreaming)
		return;

	ARemotePlayMonitor* Monitor = ARemotePlayMonitor::Instantiate(GetWorld());
	if(Monitor && Monitor->VideoEncodeFrequency > 1)
	{
		static int u = 1;
		u--;
		u = std::min(Monitor->VideoEncodeFrequency, u);
		if(!u)
		{
			u = Monitor->VideoEncodeFrequency;
		}
		else
		{
			return;
		}
	}

	if(Monitor->bDoCubemapCulling)
	{
		CullHiddenCubeSegments();
	}

	// Aidan: The parent function belongs to SceneCaptureComponentCube and is located in SceneCaptureComponent.cpp. 
	// The parent function calls UpdateSceneCaptureContents function in SceneCaptureRendering.cpp.
	// UpdateSceneCaptureContents enqueues the rendering commands to render to the scene capture cube's render target.
	// The parent function is called from the static function UpdateDeferredCaptures located in
	// SceneCaptureComponent.cpp. UpdateDeferredCaptures is called by the BeginRenderingViewFamily function in SceneRendering.cpp.
	// Therefore the rendering commands queued after this function call below directly follow the scene capture cube's commands in the queue.
	Super::UpdateSceneCaptureContents(Scene);

	if(TextureTarget && UnrealCasterContext)
	{
		if(!UnrealCasterContext->EncodePipeline)
		{
			UnrealCasterContext->EncodePipeline.reset(new FEncodePipelineMonoscopic);
			UnrealCasterContext->EncodePipeline->Initialize(EncodeParams, UnrealCasterContext, Monitor, UnrealCasterContext->ColorQueue.get(), UnrealCasterContext->DepthQueue.get());

			if(RemotePlayReflectionCaptureComponent)
			{
				RemotePlayReflectionCaptureComponent->Initialize();
				RemotePlayReflectionCaptureComponent->bAttached = true;
			}
		}
		FTransform Transform = GetComponentTransform();

		UnrealCasterContext->EncodePipeline->PrepareFrame(Scene, TextureTarget, Transform, QuadsToRender);
		if(RemotePlayReflectionCaptureComponent && EncodeParams.bDecomposeCube)
		{
			RemotePlayReflectionCaptureComponent->UpdateContents(
				Scene->GetRenderScene(),
				TextureTarget,
				Scene->GetFeatureLevel());
			int32 W = TextureTarget->GetSurfaceWidth();
			FIntPoint Offset0((W * 3) / 2, W * 2);
			RemotePlayReflectionCaptureComponent->PrepareFrame(
				Scene->GetRenderScene(),
				UnrealCasterContext->EncodePipeline->GetSurfaceTexture(),
				Scene->GetFeatureLevel(), Offset0);
		}
		UnrealCasterContext->EncodePipeline->EncodeFrame(Scene, TextureTarget, Transform, bSendKeyframe);
		// The client must request it again if it needs it
		bSendKeyframe = false;
	}
}

bool URemotePlayCaptureComponent::ShouldRenderFace(int32 FaceId) const
{
	if (FacesToRender.Num() <= FaceId)
	{
		return true;
	}
	return FacesToRender[FaceId];
}

void URemotePlayCaptureComponent::CullHiddenCubeSegments()
{
	assert(CubeQuads.Num >= 6);

	// Aidan: Currently not going to do this on GPU because doing it on game thread allows us to  
	// share the output with the capture component to cull faces from rendering.
	ARemotePlayMonitor *Monitor = ARemotePlayMonitor::Instantiate(GetWorld());

	FQuat UnrealOrientation = FQuat(ClientCamInfo.orientation.x, ClientCamInfo.orientation.y, ClientCamInfo.orientation.z, ClientCamInfo.orientation.w);
	const FVector LookAt = UnrealOrientation.GetForwardVector() * 10;
	const FVector Up = UnrealOrientation.GetUpVector();
	const FLookAtMatrix ViewMatrix = FLookAtMatrix(FVector::ZeroVector, LookAt, Up);

	// Convert FOV from degrees to radians 
	const float FOV = FMath::DegreesToRadians(ClientCamInfo.fov);
	
	FMatrix ProjectionMatrix;
	if (static_cast<int32>(ERHIZBuffer::IsInverted) == 1)
	{
		ProjectionMatrix = AdjustProjectionMatrixForRHI(FReversedZPerspectiveMatrix(FOV, ClientCamInfo.width, ClientCamInfo.height, 0, 0));
	}
	else
	{
		ProjectionMatrix = AdjustProjectionMatrixForRHI(FPerspectiveMatrix(FOV, ClientCamInfo.width, ClientCamInfo.height, 0, 0));
	}

	const FMatrix VP = ViewMatrix * ProjectionMatrix;

	// Use to prevent shared vectors from being tested more than once
	TMap<FVector, bool> VectorIntersectionMap;

	const uint32 BlocksPerFace = Monitor->BlocksPerCubeFaceAcross * Monitor->BlocksPerCubeFaceAcross;

	const FVector* Vertices = reinterpret_cast<FVector*>(&CubeQuads[0]);

	// Iterate through all six faces
	for (uint32 i = 0; i < 6; ++i)
	{
		bool FaceIntersects = false;

		// Iterate through each of the face's quads
		for (uint32 j = 0; j < BlocksPerFace; ++j)
		{
			uint32 QuadIndex = i * BlocksPerFace + j; 
			
			bool Intersects = false;
			
			// Iterate through each of the quad's vertices
			for (uint32 k = 0; k < 4; ++k)
			{
				uint32 VIndex = (QuadIndex * 4) + k;
				const auto& V = Vertices[VIndex];
				const bool* Value = VectorIntersectionMap.Find(V);
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
			
			// For debugging only! Cull only the quad selected by the user 
			if (Monitor->CullQuadIndex >= 0 && Monitor->CullQuadIndex < CubeQuads.Num())
			{
				if (QuadIndex == Monitor->CullQuadIndex)
				{
					Intersects = false;
				}
				else
				{
					Intersects = true;
				}
			}
				
			QuadsToRender[QuadIndex] = Intersects;

			if (Intersects)
			{
				FaceIntersects = true;
			}
		}
		FacesToRender[i] = FaceIntersects;
	}
}

void URemotePlayCaptureComponent::CreateCubeQuads(TArray<FQuad>& Quads, uint32 BlocksPerFaceAcross, float CubeWidth)
{
	const float HalfWidth = CubeWidth / 2;
	const float QuadSize = CubeWidth / (float)BlocksPerFaceAcross;

	// Unreal Engine coordinates: X is forward, Y is right, Z is up, 
	const FVector StartPos = FVector(HalfWidth, -HalfWidth, -HalfWidth); // Bottom left of front face

	// Aidan: First qauternion is rotated to match Unreal's cubemap face rotations
	// Second quaternion is to get position, forward and side vectors relative to front face
	// In quaternion multiplication, the rhs or second qauternion is applied first
	static const float Rad90 = FMath::DegreesToRadians(90);
	static const float Rad180 = FMath::DegreesToRadians(180);
	static const FQuat FrontQuat = FQuat(FVector::ForwardVector, -Rad90); // No need to multiply as second qauternion would be identity
	static const FQuat BackQuat = (FQuat(FVector::ForwardVector, Rad90) * FQuat(FVector::UpVector, Rad180)).GetNormalized(SMALL_NUMBER);
	static const FQuat RightQuat = FQuat(FVector::RightVector, Rad180) * FQuat(FVector::UpVector, Rad90).GetNormalized(SMALL_NUMBER);
	static const FQuat LeftQuat = FQuat(FVector::UpVector, -Rad90); // No need to multiply as first quaternion would be identity
	static const FQuat TopQuat = FQuat(FVector::UpVector, -Rad90) * FQuat(FVector::RightVector, -Rad90).GetNormalized(SMALL_NUMBER);
	static const FQuat BottomQuat = FQuat(FVector::UpVector, Rad90) * FQuat(FVector::RightVector, Rad90).GetNormalized(SMALL_NUMBER);

	static const FQuat FaceQuats[6] = { FrontQuat, BackQuat, RightQuat, LeftQuat, TopQuat, BottomQuat };

	const uint32 NumQuads = BlocksPerFaceAcross * BlocksPerFaceAcross * 6;

	Quads.Empty();
	Quads.Reserve(NumQuads);

	// Iterate through all six faces
	for (uint32 i = 0; i < 6; ++i)
	{
		const FQuat& q = FaceQuats[i];
		const FVector RightVec = q.RotateVector(FVector::RightVector) * QuadSize;
		const FVector UpVec = q.RotateVector(FVector::UpVector) * QuadSize;
		FVector Pos = q.RotateVector(StartPos);

		// Go right
		for (uint32 j = 0; j < BlocksPerFaceAcross; ++j)
		{
			FVector QuadPos = Pos;
			// Go up
			for (uint32 k = 0; k < BlocksPerFaceAcross; ++k)
			{
				FQuad Quad;
				Quad.BottomLeft = QuadPos;
				Quad.TopLeft = QuadPos + UpVec;
				Quad.BottomRight = QuadPos + RightVec;
				Quad.TopRight = Quad.TopLeft + RightVec;

				QuadPos = Quad.TopLeft;

				Quads.Emplace(MoveTemp(Quad));
			}
			Pos += RightVec;
		}
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

	// Move from projection space to normalized 0..1 UI space
	/*const float NormX = (Result.X / 2.f) + 0.5f;
	const float NormY = 1.f - (Result.Y / 2.f) - 0.5f;

	if (NormX < 0.0f || NormX > 1.0f || NormY < 0.0f || NormY > 1.0f)
	{
		return false;
	}*/

	if (Result.X < -1.0f || Result.X > 1.0f || Result.Y < -1.0f || Result.Y > 1.0f)
	{
		return false;
	}
	return true;
}

void URemotePlayCaptureComponent::startStreaming(SCServer::CasterContext* context)
{
	UnrealCasterContext = static_cast<FUnrealCasterContext*>(context);

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

	FQuat UnrealOrientation = GetComponentTransform().GetRotation();
	ClientCamInfo.orientation = {UnrealOrientation.X, UnrealOrientation.Y, UnrealOrientation.Z, UnrealOrientation.W};

	CreateCubeQuads(CubeQuads, Monitor->BlocksPerCubeFaceAcross, TextureTarget->GetSurfaceWidth());

	QuadsToRender.Init(true, CubeQuads.Num());
	FacesToRender.Init(true, 6);
}

void URemotePlayCaptureComponent::stopStreaming()
{
	bIsStreaming = false;
	bCaptureEveryFrame = false;
	CubeQuads.Empty();
	QuadsToRender.Empty();
	FacesToRender.Empty();

	if (!UnrealCasterContext)
	{
		UE_LOG(LogRemotePlay, Warning, TEXT("Capture: null UnrealCasterContext"));
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

	UnrealCasterContext = nullptr;

	UE_LOG(LogRemotePlay, Log, TEXT("Capture: Stopped streaming"));
}

void URemotePlayCaptureComponent::requestKeyframe()
{
	bSendKeyframe = true;
}

void URemotePlayCaptureComponent::OnViewportDrawn()
{
}

SCServer::CameraInfo& URemotePlayCaptureComponent::getClientCameraInfo()
{
	return ClientCamInfo;
}

