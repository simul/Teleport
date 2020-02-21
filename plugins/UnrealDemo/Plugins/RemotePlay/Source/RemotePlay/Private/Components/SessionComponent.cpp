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

#include "SimulCasterServer/CasterContext.h"

#include "Components/RemotePlayCaptureComponent.h"
#include "Components/StreamableGeometryComponent.h"
#include "RemotePlayModule.h"
#include "RemotePlayMonitor.h"
#include "RemotePlaySettings.h"

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

const avs::uid DUD_CLIENT_ID = 0;

URemotePlaySessionComponent::URemotePlaySessionComponent()
	: bAutoStartSession(true)
	, AutoListenPort(10500)
	, AutoDiscoveryPort(10607)
	, DisconnectTimeout(1000)
	, InputTouchSensitivity(1.0f)
	, GeometryStreamingService(std::make_shared<FGeometryStreamingService>())
	, DiscoveryService(std::make_shared<FRemotePlayDiscoveryService>(ARemotePlayMonitor::GetCasterSettings()))
	, ClientMessaging(std::make_unique<SCServer::ClientMessaging>(ARemotePlayMonitor::GetCasterSettings(), DiscoveryService, GeometryStreamingService,
																  std::bind(&URemotePlaySessionComponent::SetHeadPose, this, std::placeholders::_2),
																  [](avs::uid, int index, const avs::HeadPose*){},
																  std::bind(&URemotePlaySessionComponent::ProcessNewInput, this, std::placeholders::_2),
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

	GeometryStreamingService->initialise(IRemotePlay::Get().GetGeometrySource());
}

void URemotePlaySessionComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	StopSession();

	Super::EndPlay(Reason);
}

void URemotePlaySessionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	if(!ClientMessaging->hasHost() || !PlayerController.IsValid()) return;

	if(PlayerPawn.IsValid() && Monitor->bStreamGeometry)
	{
		if(!DetectionSphereInner.IsValid())
		{
			AddDetectionSpheres();
		}

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
			if(CasterContext && CasterContext->NetworkPipeline)
				Bandwidth += 0.1f * CasterContext->NetworkPipeline->getBandWidthKPS();
			FScopeBandwidth Context(BandwidthStatID, Bandwidth);
		}
		if(PlayerPawn != PlayerController->GetPawn())
		{
			if(PlayerController->GetPawn()) SwitchPlayerPawn(PlayerController->GetPawn());
		}
		if(CasterContext && CasterContext->NetworkPipeline)
		{
			CasterContext->NetworkPipeline->process();
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
		if(CasterContext && CasterContext->NetworkPipeline)
		{
			auto* pipeline = CasterContext->NetworkPipeline->getAvsPipeline();
			if(pipeline)
			{
				GEngine->AddOnScreenDebugMessage(135, 1.0f, FColor::White, FString::Printf(TEXT("Start Timestamp %d"), pipeline->GetStartTimestamp()));
				GEngine->AddOnScreenDebugMessage(137, 1.0f, FColor::White, FString::Printf(TEXT("Currt Timestamp %d"), pipeline->GetTimestamp()));
			}
		}
	}
}

void URemotePlaySessionComponent::StartSession(int32 ListenPort, int32 DiscoveryPort)
{
	if(!PlayerController.IsValid() || !PlayerController->IsLocalController()) return;
	if(Monitor->ResetCache) GeometryStreamingService->reset();

	ClientMessaging->startSession(DUD_CLIENT_ID, ListenPort);

	if(DiscoveryPort > 0)
	{
		if(!DiscoveryService->initialise(DiscoveryPort, ListenPort))
		{
			UE_LOG(LogRemotePlay, Warning, TEXT("Session: Failed to initialise discovery service!"));
		}
	}
	else
	{
		UE_LOG(LogRemotePlay, Warning, TEXT("Session: Failed to initialise discovery service! Discovery Port was: %d."), DiscoveryPort);
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
		if(Monitor->bStreamGeometry)
		{
			AddDetectionSpheres();
		}

		StartStreaming();
	}
}
void URemotePlaySessionComponent::StartStreaming()
{
	URemotePlayCaptureComponent* CaptureComponent = Cast<URemotePlayCaptureComponent>(PlayerPawn->GetComponentByClass(URemotePlayCaptureComponent::StaticClass()));
	if (!CaptureComponent)
		return;
	
	delete CasterContext;
	CasterContext = new SCServer::CasterContext;
	CasterContext->ColorQueue.reset(new avs::Queue);
	CasterContext->ColorQueue->configure(16);

#if 0

	// Roderick: with consumer GPU's we can't have more than one video stream.
	// So we're encoding depth as alpha: no need for a separate source.
	if (CaptureComponent->CaptureSource == ESceneCaptureSource::SCS_SceneColorSceneDepth)
	{
		CasterContext->bCaptureDepth = true;
		CasterContext->DepthQueue.Reset(new avs::Queue); 
		CasterContext->DepthQueue->configure(16); 
	}
	else
#endif
	{
		CasterContext->isCapturingDepth = false;
	}
	CasterContext->GeometryQueue.reset(new avs::Queue);
	CasterContext->GeometryQueue->configure(16);

	
	ClientMessaging->initialise
	(
		CasterContext,
		{std::bind(&URemotePlayCaptureComponent::startStreaming, CaptureComponent, std::placeholders::_1), std::bind(&URemotePlayCaptureComponent::requestKeyframe, CaptureComponent), std::bind(&URemotePlayCaptureComponent::getClientCameraInfo, CaptureComponent)}
	);

	const FUnrealCasterEncoderSettings& EncoderSettings = CaptureComponent->GetEncoderSettings();
	avs::SetupCommand setupCommand;
	setupCommand.port = ClientMessaging->getServerPort() + 1;
	setupCommand.video_width	= EncoderSettings.FrameWidth;
	setupCommand.video_height	= EncoderSettings.FrameHeight;
	setupCommand.depth_height	= EncoderSettings.DepthHeight;
	setupCommand.depth_width	= EncoderSettings.DepthWidth;
	setupCommand.use_10_bit_decoding = Monitor->bUse10BitEncoding;
	setupCommand.use_yuv_444_decoding = Monitor->bUseYUV444Decoding;
	setupCommand.colour_cubemap_size = EncoderSettings.FrameWidth / 3;
	setupCommand.compose_cube	= EncoderSettings.bDecomposeCube;
	setupCommand.debug_stream=Monitor->DebugStream;
	setupCommand.do_checksums = Monitor->Checksums ? 1 : 0;
	setupCommand.debug_network_packets=Monitor->DebugNetworkPackets;
	setupCommand.requiredLatencyMs = Monitor->RequiredLatencyMs;
	setupCommand.server_id = Monitor->GetServerID();
	setupCommand.is_bgr = true;

	UE_CLOG(!EncoderSettings.bDecomposeCube, LogRemotePlay, Warning, TEXT("'Decompose Cube' is set to false on %s's capture component; this may cause a black video output on the client."), *GetOuter()->GetName());

	//If this is a reconnect we don't want the client throwing away resources it will need, so we send a list of resources it will need; but only if we're actually streaming geometry.
	if(Monitor->bStreamGeometry)
	{
		//Fill the list of streamed actors, so a reconnecting client will not have to download geometry it already has.
		TSet<AActor*> actorsOverlappingOnStart;
		DetectionSphereInner->GetOverlappingActors(actorsOverlappingOnStart);
		for(AActor* actor : actorsOverlappingOnStart)
		{
			GeometryStreamingService->addActor(actor);
		}	
	}

	ClientMessaging->sendSetupCommand(std::move(setupCommand));
	IsStreaming = true;
}

void URemotePlaySessionComponent::StopStreaming()
{
	//Stop geometry stream.
	GeometryStreamingService->stopStreaming();

	//Stop video stream.
	if(PlayerPawn.IsValid())
	{
		URemotePlayCaptureComponent* CaptureComponent = Cast<URemotePlayCaptureComponent>(PlayerPawn->GetComponentByClass(URemotePlayCaptureComponent::StaticClass()));
		if(CaptureComponent)
		{
			CaptureComponent->stopStreaming();
		}

		PlayerPawn.Reset();
	}

	//End connection.
	if (ClientMessaging->hasPeer())
	{
		avs::ShutdownCommand shutdownCommand;
		ClientMessaging->sendCommand(shutdownCommand);
	}

	//Clean-up caster context.
	//This should happen last as an attempt to use the queues may occur part-way through shutdown; for example, the video stream will try to queue data onto the colour queue.
	if (CasterContext)
	{
		if (CasterContext->NetworkPipeline)
		{
			CasterContext->NetworkPipeline->release();
			CasterContext->NetworkPipeline.reset();
		}
		CasterContext->ColorQueue.reset();
		CasterContext->DepthQueue.reset();
		CasterContext->GeometryQueue.reset();
	}
	
	delete CasterContext;
	CasterContext = nullptr;
	IsStreaming = false;
}

void URemotePlaySessionComponent::OnInnerSphereBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if(IsStreaming)
	{
		avs::uid actorID = GeometryStreamingService->addActor(OtherActor);
		if(actorID != 0)
		{
			//Don't tell the client to show an actor it has yet to receive.
			if(!GeometryStreamingService->hasResource(actorID)) return;

			ClientMessaging->actorEnteredBounds(actorID);

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
		avs::uid actorID = GeometryStreamingService->removeActor(OtherActor);
		if(actorID != 0)
		{
			ClientMessaging->actorLeftBounds(actorID);

			UE_LOG(LogRemotePlay, Verbose, TEXT("\"%s\" ended overlap with actor \"%s\"."), *OverlappedComponent->GetName(), *OtherActor->GetName());
		}
	}
}

void URemotePlaySessionComponent::AddDetectionSpheres()
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
 
void URemotePlaySessionComponent::SetHeadPose(const avs::HeadPose* newHeadPose)
{
	if(!PlayerController.Get() || !PlayerPawn.Get()) return;

	URemotePlayCaptureComponent* CaptureComponent = Cast<URemotePlayCaptureComponent>(PlayerPawn->GetComponentByClass(URemotePlayCaptureComponent::StaticClass()));
	if(!CaptureComponent) return;
	
	avs::vec3 position = newHeadPose->position;
	avs::vec4 orientation = newHeadPose->orientation;
	
	FVector OldActorPos = PlayerPawn->GetActorLocation();
	//Convert back to millimetres.
	FVector NewCameraPos = FVector(position.x, position.y, position.z) * 100.0f;

	//We want the relative location between the player and the camera to stay the same, and the player's Z component to be unchanged.
	FVector ActorToComponent = CaptureComponent->GetComponentLocation() - OldActorPos;
	FVector newActorPos = NewCameraPos - ActorToComponent;
	newActorPos.Z = OldActorPos.Z;
	PlayerPawn->SetActorLocation(newActorPos);

	// Here we set the angle of the player pawn.
	const FQuat HeadPoseUE(orientation.x, orientation.y, orientation.z, orientation.w);
	PlayerController->SetControlRotation(HeadPoseUE.Rotator());		

	SCServer::CameraInfo& ClientCamInfo = CaptureComponent->getClientCameraInfo();
	ClientCamInfo.position = {NewCameraPos.X, NewCameraPos.Y, NewCameraPos.Z};
	ClientCamInfo.orientation = orientation;

	CaptureComponent->SetWorldLocation(NewCameraPos);

	if(Monitor->GetCasterSettings()->enableDebugControlPackets)
	{
		static char c = 0;
		c--;
		if(!c)
		{
			UE_LOG(LogRemotePlay, Warning, TEXT("Received Head Pos: %3.2f %3.2f %3.2f"), position.x, position.y, position.z);
		}
	}
}

void URemotePlaySessionComponent::ProcessNewInput(const avs::InputState* newInput)
{
	InputTouchAxis.X = FMath::Clamp(newInput->trackpadAxisX * InputTouchSensitivity, -1.0f, 1.0f);
	InputTouchAxis.Y = FMath::Clamp(newInput->trackpadAxisY * InputTouchSensitivity, -1.0f, 1.0f);
	InputJoystick.X = newInput->joystickAxisX;
	InputJoystick.Y = newInput->joystickAxisY;
	TranslateButtons(newInput->buttonsPressed, InputQueue.ButtonsPressed);
	TranslateButtons(newInput->buttonsReleased, InputQueue.ButtonsReleased);
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
