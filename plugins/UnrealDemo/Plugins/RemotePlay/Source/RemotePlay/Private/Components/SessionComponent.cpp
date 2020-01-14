// Copyright 2018 Simul.co
#define WIN32_LEAN_AND_MEAN

#include "SessionComponent.h"

#include <algorithm> //std::remove

#include "Engine/Classes/Components/SphereComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"

#include "enet/enet.h"
#include "libavstream/common.hpp"

#include "Components/RemotePlayCaptureComponent.h"
#include "Components/StreamableGeometryComponent.h"
#include "RemotePlayModule.h"
#include "RemotePlayMonitor.h"
#include "RemotePlaySettings.h"
#include "UnrealCasterContext.h"

DECLARE_STATS_GROUP(TEXT("RemotePlay_Game"), STATGROUP_RemotePlay, STATCAT_Advanced);

template< typename TStatGroup>
static TStatId CreateStatId(const FName StatNameOrDescription, EStatDataType::Type dataType)
{ 
#if	STATS
	FString Description;
	StatNameOrDescription.ToString(Description);
	FStartupMessages::Get().AddMetadata(StatNameOrDescription, *Description,
		TStatGroup::GetGroupName(),
		TStatGroup::GetGroupCategory(), 
		TStatGroup::GetDescription(),
		false,dataType, false, false);
	TStatId StatID = IStatGroupEnableManager::Get().GetHighPerformanceEnableForStat(StatNameOrDescription,
		TStatGroup::GetGroupName(),
		TStatGroup::GetGroupCategory(),
		TStatGroup::DefaultEnable,
		false, dataType, *Description, false, false);

	return StatID;
#endif // STATS

	return TStatId();
}
template< typename TStatGroup >
static TStatId CreateStatId(const FString& StatNameOrDescription, EStatDataType::Type dataType)
{
#if	STATS
	return CreateStatId<TStatGroup>(FName(*StatNameOrDescription),dataType);
#endif // STATS

	return TStatId();
}

/**
 * This is a utility class for counting the number of cycles during the
 * lifetime of the object. It creates messages for the stats thread.
 */
class FScopeBandwidth
{
	/** Name of the stat, usually a short name **/
	FName StatId;

public:

	/**
	 * Pushes the specified stat onto the hierarchy for this thread. Starts
	 * the timing of the cycles used
	 */
	 FScopeBandwidth(TStatId InStatId, float bandwidth)
	{
		FMinimalName StatMinimalName = InStatId.GetMinimalName(EMemoryOrder::Relaxed);
		if (StatMinimalName.IsNone())
		{
			return;
		}
		if ( FThreadStats::IsCollectingData())
		{
			FName StatName = MinimalNameToName(StatMinimalName);
			StatId = StatName;
			FThreadStats::AddMessage(StatName, EStatOperation::Set, double(bandwidth));
		}
	}
	 ~FScopeBandwidth()
	{
		if (!StatId.IsNone())
		{
			//FThreadStats::AddMessage(StatId, EStatOperation::CycleScopeEnd);
		}
	}
};

URemotePlaySessionComponent::URemotePlaySessionComponent()
	: bAutoStartSession(true)
	, AutoListenPort(10500)
	, AutoDiscoveryPort(10607)
	, DisconnectTimeout(1000)
	, InputTouchSensitivity(1.0f)
	, DiscoveryService(std::make_shared<FRemotePlayDiscoveryService>(ARemotePlayMonitor::GetCasterSettings()))
	, ClientMessaging(std::make_unique<SCServer::ClientMessaging>(DiscoveryService, GeometryStreamingService, ARemotePlayMonitor::GetCasterSettings(),
																  std::bind(&URemotePlaySessionComponent::SetHeadPose, this, std::placeholders::_1),
																  std::bind(&URemotePlaySessionComponent::ProcessNewInput, this, std::placeholders::_1),
																  std::bind(&URemotePlaySessionComponent::StopStreaming, this),
																  DisconnectTimeout))
	, InputTouchAxis(0.f, 0.f)
	, InputJoystick(0.f,0.f)
	, BandwidthStatID(0)
	, Bandwidth(0.0f)
{
	PrimaryComponentTick.bCanEverTick = true;
}

void URemotePlaySessionComponent::BeginPlay()
{
	Super::BeginPlay();

	Monitor=ARemotePlayMonitor::Instantiate(GetWorld());
	Bandwidth = 0.0f;
	//INC_DWORD_STAT(STAT_BANDWIDTH); //Increments the counter by one each call.
#if STATS
	FString BandwidthName = GetName() + " Bandwidth kps";
	BandwidthStatID = CreateStatId<FStatGroup_STATGROUP_RemotePlay>(BandwidthName, EStatDataType::ST_double);
#endif // ENABLE_STATNAMEDEVENTS

	PlayerController = Cast<APlayerController>(GetOuter());
	if(!PlayerController.IsValid())
	{
		UE_LOG(LogRemotePlay, Error, TEXT("Session: Session component must be attached to a player controller!"));
		return;
	}

	if(bAutoStartSession)
	{
		StartSession(AutoListenPort, AutoDiscoveryPort);
	}

	GeometryStreamingService.initialise(IRemotePlay::Get().GetGeometrySource());
}

void URemotePlaySessionComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	StopSession();

	Super::EndPlay(Reason);
}

void URemotePlaySessionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	if(!ClientMessaging->hasHost() || !PlayerController.IsValid()) return;

	if(PlayerPawn.IsValid() && Monitor->StreamGeometry)
	{
		DetectionSphereInner->SetSphereRadius(Monitor->DetectionSphereRadius);
		DetectionSphereOuter->SetSphereRadius(Monitor->DetectionSphereRadius + Monitor->DetectionSphereBufferDistance);
	}

	if(ClientMessaging->hasPeer())
	{
		if(BandwidthStatID.IsValidStat())
		{
			//SET_FLOAT_STAT(StatID, 50.0f);
			//if (FThreadStats::IsCollectingData() )
			//	FThreadStats::AddMessage(GET_STATFNAME(Stat), EStatOperation::Set, double(Value));
			Bandwidth *= 0.9f;
			if(UnrealCasterContext && UnrealCasterContext->NetworkPipeline)
				Bandwidth += 0.1f * UnrealCasterContext->NetworkPipeline->getBandWidthKPS();
			FScopeBandwidth Context(BandwidthStatID, Bandwidth);
		}
		if(PlayerPawn != PlayerController->GetPawn())
		{
			if(PlayerController->GetPawn())
				SwitchPlayerPawn(PlayerController->GetPawn());
		}
		if(UnrealCasterContext && UnrealCasterContext->NetworkPipeline)
		{
			UnrealCasterContext->NetworkPipeline->process();
		}

		ClientMessaging->tick(DeltaTime);
	}
	else
	{
		DiscoveryService->tick();
	}

	ClientMessaging->handleEvents();
	ApplyPlayerInput(DeltaTime);

	if (GEngine)
	{
		if (UnrealCasterContext&&UnrealCasterContext->NetworkPipeline)
		{
			auto *pipeline=UnrealCasterContext->NetworkPipeline->getAvsPipeline();
			if (pipeline)
			{
				GEngine->AddOnScreenDebugMessage(135, 1.0f, FColor::White, FString::Printf(TEXT("Start Timestamp %d"), pipeline->GetStartTimestamp()));
				GEngine->AddOnScreenDebugMessage(137, 1.0f, FColor::White, FString::Printf(TEXT("Currt Timestamp %d"), pipeline->GetTimestamp()));
			}
		}
	}
}

void URemotePlaySessionComponent::StartSession(int32 ListenPort, int32 DiscoveryPort)
{
	if (!PlayerController.IsValid() || !PlayerController->IsLocalController()) return;
	if(Monitor->ResetCache) GeometryStreamingService.reset();

	ClientMessaging->startSession(ListenPort);

	if (DiscoveryPort > 0)
	{
		if (!DiscoveryService->initialise(DiscoveryPort, ListenPort))
		{
			UE_LOG(LogRemotePlay, Warning, TEXT("Session: Failed to initialize discovery service"));
		}
	}
}

void URemotePlaySessionComponent::StopSession()
{
	StopStreaming();
	DiscoveryService->shutdown();
	ClientMessaging->stopSession();
}

void URemotePlaySessionComponent::SwitchPlayerPawn(APawn* NewPawn)
{
	assert(ClientMessaging->hasPeer());

	StopStreaming();
	PlayerPawn = NewPawn;

	if (PlayerPawn.IsValid())
	{
		//Attach detection spheres to player pawn, but only if we're actually streaming geometry.
		if(Monitor->StreamGeometry)
		{
			FAttachmentTransformRules transformRules = FAttachmentTransformRules(EAttachmentRule::KeepRelative, true);

			//Attach streamable geometry detection spheres to player pawn.
			{
				DetectionSphereInner = NewObject<USphereComponent>(PlayerPawn.Get(), "InnerSphere");
				DetectionSphereInner->OnComponentBeginOverlap.AddDynamic(this, &URemotePlaySessionComponent::OnInnerSphereBeginOverlap);
				DetectionSphereInner->SetCollisionProfileName("RemotePlaySensor");
				DetectionSphereInner->SetGenerateOverlapEvents(true);
				DetectionSphereInner->SetSphereRadius(Monitor->DetectionSphereRadius);

				DetectionSphereInner->RegisterComponent();
				DetectionSphereInner->AttachToComponent(PlayerPawn->GetRootComponent(), transformRules);
			}

			{
				DetectionSphereOuter = NewObject<USphereComponent>(PlayerPawn.Get(), "OuterSphere");
				DetectionSphereOuter->OnComponentEndOverlap.AddDynamic(this, &URemotePlaySessionComponent::OnOuterSphereEndOverlap);
				DetectionSphereOuter->SetCollisionProfileName("RemotePlaySensor");
				DetectionSphereOuter->SetGenerateOverlapEvents(true);
				DetectionSphereOuter->SetSphereRadius(Monitor->DetectionSphereRadius + Monitor->DetectionSphereBufferDistance);

				DetectionSphereOuter->RegisterComponent();
				DetectionSphereOuter->AttachToComponent(PlayerPawn->GetRootComponent(), transformRules);
			}
		}

		StartStreaming();
	}
}
void URemotePlaySessionComponent::StartStreaming()
{
	URemotePlayCaptureComponent* CaptureComponent = Cast<URemotePlayCaptureComponent>(PlayerPawn->GetComponentByClass(URemotePlayCaptureComponent::StaticClass()));
	if (!CaptureComponent)
		return;
	
	delete UnrealCasterContext;
	UnrealCasterContext = new FUnrealCasterContext;
	UnrealCasterContext->ColorQueue.reset(new avs::Queue);
	UnrealCasterContext->ColorQueue->configure(16);

#if 0

	// Roderick: with consumer GPU's we can't have more than one video stream.
	// So we're encoding depth as alpha: no need for a separate source.
	if (CaptureComponent->CaptureSource == ESceneCaptureSource::SCS_SceneColorSceneDepth)
	{
		UnrealCasterContext->bCaptureDepth = true;
		UnrealCasterContext->DepthQueue.Reset(new avs::Queue); 
		UnrealCasterContext->DepthQueue->configure(16); 
	}
	else
#endif
	{
		UnrealCasterContext->isCapturingDepth = false;
	}
	UnrealCasterContext->GeometryQueue.reset(new avs::Queue);
	UnrealCasterContext->GeometryQueue->configure(16);

	ClientMessaging->initialise(UnrealCasterContext, CaptureComponent);

	const FUnrealCasterEncoderSettings& EncoderSettings = CaptureComponent->GetEncoderSettings();
	const int32 StreamingPort = ClientMessaging->getServerPort() + 1;
	avs::SetupCommand setupCommand;
	setupCommand.video_width	= EncoderSettings.FrameWidth;
	setupCommand.video_height	= EncoderSettings.FrameHeight;
	setupCommand.depth_height	= EncoderSettings.DepthHeight;
	setupCommand.depth_width	= EncoderSettings.DepthWidth;
	setupCommand.colour_cubemap_size = EncoderSettings.FrameWidth / 3;
	setupCommand.compose_cube	= EncoderSettings.bDecomposeCube;
	setupCommand.port = StreamingPort; 
	setupCommand.debug_stream=Monitor->DebugStream;
	setupCommand.debug_network_packets=Monitor->DebugNetworkPackets;
	setupCommand.do_checksums = Monitor->Checksums?1:0;
	setupCommand.server_id = Monitor->GetServerID();
	setupCommand.use_10_bit_decoding = Monitor->bUse10BitEncoding;
	setupCommand.use_yuv_444_decoding = Monitor->bUseYUV444Decoding;
	setupCommand.requiredLatencyMs=Monitor->RequiredLatencyMs;

	UE_CLOG(!EncoderSettings.bDecomposeCube, LogRemotePlay, Warning, TEXT("'Decompose Cube' is set to false on %s's capture component; this may cause a black video output on the client."), *GetOuter()->GetName());

	std::vector<avs::uid> resourcesClientNeeds;
	//If this is a reconnect we don't want the client throwing away resources it will need, so we send a list of resources it will need; but only if we're actually streaming geometry.
	if(Monitor->StreamGeometry)
	{
		//Fill the list of streamed actors, so a reconnecting client will not have to download geometry it already has.
		TSet<AActor*> actorsOverlappingOnStart;
		DetectionSphereInner->GetOverlappingActors(actorsOverlappingOnStart);
		for(AActor* actor : actorsOverlappingOnStart)
		{
			GeometryStreamingService.addActor(actor);
		}

		//Get resources the client will need to check it has.
		std::vector<avs::MeshNodeResources> outMeshResources;
		std::vector<avs::LightNodeResources> outLightResources;
		GeometryStreamingService.getResourcesToStream(outMeshResources, outLightResources);

		for(const avs::MeshNodeResources& meshResource : outMeshResources)
		{
			resourcesClientNeeds.push_back(meshResource.node_uid);
			resourcesClientNeeds.push_back(meshResource.mesh_uid);

			for(const avs::MaterialResources& material : meshResource.materials)
			{
				resourcesClientNeeds.push_back(material.material_uid);

				for(avs::uid texture_uid : material.texture_uids)
				{
					resourcesClientNeeds.push_back(texture_uid);
				}
			}
		}
		for(const avs::LightNodeResources& lightResource : outLightResources)
		{
			resourcesClientNeeds.push_back(lightResource.node_uid);
			resourcesClientNeeds.push_back(lightResource.shadowmap_uid);
		}

		//Remove duplicates, and UIDs of 0.
		std::sort(resourcesClientNeeds.begin(), resourcesClientNeeds.end());
		resourcesClientNeeds.erase(std::unique(resourcesClientNeeds.begin(), resourcesClientNeeds.end()), resourcesClientNeeds.end());
		resourcesClientNeeds.erase(std::remove(resourcesClientNeeds.begin(), resourcesClientNeeds.end(), 0), resourcesClientNeeds.end());

		//If the client needs a resource it will tell us; we don't want to stream the data if the client already has it.
		for(avs::uid resourceID : resourcesClientNeeds)
		{
			GeometryStreamingService.confirmResource(resourceID);
		}
	}
	setupCommand.resourceCount = resourcesClientNeeds.size();
	ClientMessaging->sendCommand<avs::uid>(setupCommand, resourcesClientNeeds);

	IsStreaming = true;
}

void URemotePlaySessionComponent::StopStreaming()
{
	GeometryStreamingService.stopStreaming();
	if (ClientMessaging->hasPeer())
	{
		avs::ShutdownCommand shutdownCommand;
		ClientMessaging->sendCommand(shutdownCommand);
	}
	if (UnrealCasterContext)
	{
		if (UnrealCasterContext->EncodePipeline)
		{
			UnrealCasterContext->EncodePipeline->Release();
			UnrealCasterContext->EncodePipeline.reset();
		}
		if (UnrealCasterContext->NetworkPipeline)
		{
			UnrealCasterContext->NetworkPipeline->release();
			UnrealCasterContext->NetworkPipeline.reset();
		}
		UnrealCasterContext->ColorQueue.reset();
		UnrealCasterContext->DepthQueue.reset();
		UnrealCasterContext->GeometryQueue.reset();
	}
	if (PlayerPawn.IsValid())
	{
		URemotePlayCaptureComponent* CaptureComponent = Cast<URemotePlayCaptureComponent>(PlayerPawn->GetComponentByClass(URemotePlayCaptureComponent::StaticClass()));
		if (CaptureComponent)
		{
			CaptureComponent->stopStreaming();
		}

		PlayerPawn.Reset();
	}
	delete UnrealCasterContext;
	UnrealCasterContext = nullptr;
	IsStreaming = false;
}

void URemotePlaySessionComponent::OnInnerSphereBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if(IsStreaming)
	{
		avs::uid actorID = GeometryStreamingService.addActor(OtherActor);
		if(actorID != 0 && IsStreaming)
		{
			//Don't tell the client to show an actor it has yet to receive.
			if(!GeometryStreamingService.hasResource(actorID)) return;

			ClientMessaging->gainedActor(actorID);

			UE_LOG(LogRemotePlay, Verbose, TEXT("\"%s\" overlapped with actor \"%s\"."), *OverlappedComponent->GetName(), *OtherActor->GetName());
		}
		else
		{
			UE_LOG(LogRemotePlay, Warning, TEXT("Actor \"%s\" overlapped with \"%s\", but the actor is not supported! Only use supported component types, and check collision Settings!"), *OverlappedComponent->GetName(), *OtherActor->GetName())
		}
	}
}

void URemotePlaySessionComponent::OnOuterSphereEndOverlap(UPrimitiveComponent * OverlappedComponent, AActor * OtherActor, UPrimitiveComponent * OtherComp, int32 OtherBodyIndex)
{
	if(IsStreaming)
	{
		avs::uid actorID = GeometryStreamingService.removeActor(OtherActor);
		if(actorID != 0)
		{
			ClientMessaging->lostActor(actorID);

			UE_LOG(LogRemotePlay, Verbose, TEXT("\"%s\" ended overlap with actor \"%s\"."), *OverlappedComponent->GetName(), *OtherActor->GetName());
		}
	}
}

void URemotePlaySessionComponent::ApplyPlayerInput(float DeltaTime)
{
	check(PlayerController.IsValid());
	static bool move_from_inputs = false;
	if (move_from_inputs)
	{
		PlayerController->InputAxis(EKeys::MotionController_Right_Thumbstick_X, InputTouchAxis.X + InputJoystick.X, DeltaTime, 1, true);
		PlayerController->InputAxis(EKeys::MotionController_Right_Thumbstick_Y, InputTouchAxis.Y + InputJoystick.Y, DeltaTime, 1, true);
	}
	while (InputQueue.ButtonsPressed.Num() > 0)
	{
		PlayerController->InputKey(InputQueue.ButtonsPressed.Pop(), EInputEvent::IE_Pressed, 1.0f, true);
	} 
	while (InputQueue.ButtonsReleased.Num() > 0)
	{
		PlayerController->InputKey(InputQueue.ButtonsReleased.Pop(), EInputEvent::IE_Released, 1.0f, true);
	}  
}

void URemotePlaySessionComponent::SetHeadPose(avs::HeadPose& newHeadPose)
{
	if(!PlayerController.Get() || !PlayerPawn.Get()) return;
	
	// Here we set the angle of the player pawn.
	// Convert quaternion from Simulcaster coordinate system (X right, Y forward, Z up) to UE4 coordinate system (left-handed, X left, Y forward, Z up).
	avs::ConvertRotation(UnrealCasterContext->axesStandard, avs::AxesStandard::UnrealStyle, newHeadPose.orientation);
	avs::ConvertPosition(UnrealCasterContext->axesStandard, avs::AxesStandard::UnrealStyle, newHeadPose.position);
	FVector NewCameraPos(newHeadPose.position.x, newHeadPose.position.y, newHeadPose.position.z);
	// back to centimetres...
	NewCameraPos *= 100.0f;

	if(Monitor->GetCasterSettings().enableDebugControlPackets)
	{
		static char c = 0;
		c--;
		if(!c)
		{
			UE_LOG(LogRemotePlay, Warning, TEXT("Received Head Pos: %3.2f %3.2f %3.2f"), newHeadPose.position.x, newHeadPose.position.y, newHeadPose.position.z);
		}
	}
	URemotePlayCaptureComponent* CaptureComponent = Cast<URemotePlayCaptureComponent>(PlayerPawn->GetComponentByClass(URemotePlayCaptureComponent::StaticClass()));
	if(!CaptureComponent) return;

	FVector CurrentActorPos = PlayerPawn->GetActorLocation();

	//We want the Relative Location x and y to remain the same, while the z may vary.
	FVector ActorToComponent = CaptureComponent->GetComponentLocation() - CurrentActorPos;

	FVector newActorPos = NewCameraPos - ActorToComponent;
	newActorPos.Z = CurrentActorPos.Z;
	PlayerController->GetPawn()->SetActorLocation(newActorPos);

	SCServer::CameraInfo& ClientCamInfo = CaptureComponent->getClientCameraInfo();

	CaptureComponent->SetWorldLocation(NewCameraPos);
	ClientCamInfo.position = {NewCameraPos.X, NewCameraPos.Y, NewCameraPos.Z};
	const FQuat HeadPoseUE(newHeadPose.orientation.x, newHeadPose.orientation.y, newHeadPose.orientation.z, newHeadPose.orientation.w);
	FVector Euler = HeadPoseUE.Euler();
	Euler.X = Euler.Y = 0.0f;
	// Unreal thinks the Euler angle starts from facing X, but actually it's Y.
	//Euler.Z += 180.0f;
	FQuat FlatPose = FQuat::MakeFromEuler(Euler);
	ClientCamInfo.orientation = newHeadPose.orientation;
	check(PlayerController.IsValid());
	PlayerController->SetControlRotation(FlatPose.Rotator());
}

void URemotePlaySessionComponent::ProcessNewInput(const avs::InputState& newInput)
{
	InputTouchAxis.X = FMath::Clamp(newInput.trackpadAxisX * InputTouchSensitivity, -1.0f, 1.0f);
	InputTouchAxis.Y = FMath::Clamp(newInput.trackpadAxisY * InputTouchSensitivity, -1.0f, 1.0f);
	InputJoystick.X = newInput.joystickAxisX;
	InputJoystick.Y = newInput.joystickAxisY;
	TranslateButtons(newInput.buttonsPressed, InputQueue.ButtonsPressed);
	TranslateButtons(newInput.buttonsReleased, InputQueue.ButtonsReleased);
}

void URemotePlaySessionComponent::TranslateButtons(uint32_t ButtonMask, TArray<FKey>& OutKeys)
{
	// TODO: Add support for other buttons as well.

	enum ERemotePlayButtons
	{
		BUTTON_A = 0x00000001,
		BUTTON_ENTER = 0x00100000,
		BUTTON_BACK = 0x00200000,
	};

	if (ButtonMask & BUTTON_A)
	{
		OutKeys.Add(EKeys::MotionController_Right_Trigger);
	}
	if (ButtonMask & BUTTON_ENTER)
	{
		// Not sure about this.
		OutKeys.Add(EKeys::Virtual_Accept);
	}
	if (ButtonMask & BUTTON_BACK)
	{
		OutKeys.Add(EKeys::Virtual_Back);
	}
}
