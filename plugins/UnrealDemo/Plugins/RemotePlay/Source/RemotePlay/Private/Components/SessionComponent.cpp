// Copyright 2018 Simul.co
#define WIN32_LEAN_AND_MEAN

#include "Components/SessionComponent.h"
#include "Components/CaptureComponent.h"
#include "Components/StreamableGeometryComponent.h"
#include "RemotePlayModule.h"
#include "RemotePlaySettings.h"
#include "RemotePlayMonitor.h"

#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
 
#include "enet/enet.h"
#include "libavstream/common.hpp"
DECLARE_STATS_GROUP(TEXT("RemotePlay_Game"), STATGROUP_RemotePlay, STATCAT_Advanced);

#include "Engine/Classes/Components/SphereComponent.h"
#include "TimerManager.h"

#include <algorithm> //std::remove

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
	, InputTouchAxis(0.f, 0.f)
	, InputJoystick(0.f,0.f)
	, ServerHost(nullptr)
	, ClientPeer(nullptr)
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

	GeometryStreamingService.Initialise(GetWorld(), IRemotePlay::Get().GetGeometrySource());
}

void URemotePlaySessionComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	StopSession();
	Super::EndPlay(Reason);
}

void URemotePlaySessionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	if (!ServerHost || !PlayerController.IsValid())
	{
		return;
	}

	if(PlayerPawn.IsValid())
	{
		DetectionSphereInner->SetSphereRadius(Monitor->DetectionSphereRadius);
		DetectionSphereOuter->SetSphereRadius(Monitor->DetectionSphereRadius + Monitor->DetectionSphereBufferDistance);
	}
	 
	if (ClientPeer)
	{
		if (BandwidthStatID.IsValidStat())
		{ 
			//SET_FLOAT_STAT(StatID, 50.0f);
			//if (FThreadStats::IsCollectingData() )
			//	FThreadStats::AddMessage(GET_STATFNAME(Stat), EStatOperation::Set, double(Value));
			Bandwidth *=0.9f;
			if (RemotePlayContext&&RemotePlayContext->NetworkPipeline.IsValid())
				Bandwidth+=0.1f*RemotePlayContext->NetworkPipeline->GetBandWidthKPS();
			FScopeBandwidth Context(BandwidthStatID, Bandwidth);
		} 
		if (PlayerPawn != PlayerController->GetPawn())
		{
			if(PlayerController->GetPawn())
				SwitchPlayerPawn(PlayerController->GetPawn());
		}
		if (RemotePlayContext&&RemotePlayContext->NetworkPipeline.IsValid())
		{
			RemotePlayContext->NetworkPipeline->Process();
		}
		if (Monitor)
		{
			GeometryStreamingService.SetStreamingContinuously(Monitor->StreamGeometryContinuously);
		}

		static float timeSinceLastGeometryStream = 0;
		timeSinceLastGeometryStream += DeltaTime;

		const float TIME_BETWEEN_GEOMETRY_TICKS = 1.0f / Monitor->GeometryTicksPerSecond;

		//Only tick the geometry streaming service a set amount of times per second.
		if(timeSinceLastGeometryStream >= TIME_BETWEEN_GEOMETRY_TICKS)
		{
			GeometryStreamingService.Tick(TIME_BETWEEN_GEOMETRY_TICKS);

			//Tell the client to change the visibility of actors that have changed whether they are within streamable bounds.
			if(!ActorsEnteredBounds.empty() || !ActorsLeftBounds.empty())
			{
				size_t commandSize = sizeof(avs::ActorBoundsCommand);
				size_t enteredBoundsSize = sizeof(avs::uid) * ActorsEnteredBounds.size();
				size_t leftBoundsSize = sizeof(avs::uid) * ActorsLeftBounds.size();

				avs::ActorBoundsCommand boundsCommand(ActorsEnteredBounds.size(), ActorsLeftBounds.size());
				ENetPacket* packet = enet_packet_create(&boundsCommand, commandSize, ENET_PACKET_FLAG_RELIABLE);

				//Resize packet, and insert actor lists.
				enet_packet_resize(packet, commandSize + enteredBoundsSize + leftBoundsSize);
				memcpy(packet->data + commandSize, ActorsEnteredBounds.data(), enteredBoundsSize);
				memcpy(packet->data + commandSize + enteredBoundsSize, ActorsLeftBounds.data(), leftBoundsSize);

				enet_peer_send(ClientPeer, static_cast<enet_uint8>(avs::RemotePlaySessionChannel::RPCH_Control), packet);

				ActorsEnteredBounds.clear();
				ActorsLeftBounds.clear();
			}

			timeSinceLastGeometryStream -= TIME_BETWEEN_GEOMETRY_TICKS;
		}
	}
	else
	{
		DiscoveryService.Tick();
	}

	ENetEvent Event;
	while (enet_host_service(ServerHost, &Event, 0) > 0)
	{
		switch (Event.type)
		{
		case ENET_EVENT_TYPE_CONNECT:
			check(ClientPeer == nullptr);
			char IPAddr[20];
			enet_address_get_host_ip(&Event.peer->address, IPAddr, sizeof(IPAddr));
			ClientPeer = Event.peer;
			DiscoveryService.Shutdown();
			UE_LOG(LogRemotePlay, Log, TEXT("Client connected: %s:%d"), *Client_GetIPAddress(), Client_GetPort());
			break;
		case ENET_EVENT_TYPE_DISCONNECT:
			check(ClientPeer == Event.peer);
			UE_LOG(LogRemotePlay, Log, TEXT("Client disconnected: %s:%d"), *Client_GetIPAddress(), Client_GetPort());
			ReleasePlayerPawn();
			// TRY to restart the discovery service...
			DiscoveryService.Initialize(Monitor);
			ClientPeer = nullptr;
			break;
		case ENET_EVENT_TYPE_RECEIVE:
			DispatchEvent(Event);
			break;
		}
	}
	if (GEngine)
	{
	//	avs::Context::instance()->
		if (RemotePlayContext&&RemotePlayContext->NetworkPipeline.IsValid())
		{
			auto *pipeline=RemotePlayContext->NetworkPipeline->GetAvsPipeline();
			if (pipeline)
			{
				GEngine->AddOnScreenDebugMessage(135, 1.0f, FColor::White, FString::Printf(TEXT("Start Timestamp %d"), pipeline->GetStartTimestamp()));
				GEngine->AddOnScreenDebugMessage(137, 1.0f, FColor::White, FString::Printf(TEXT("Currt Timestamp %d"), pipeline->GetTimestamp()));
			}
		}
	}
	ApplyPlayerInput(DeltaTime);
}

void URemotePlaySessionComponent::StartSession(int32 ListenPort, int32 DiscoveryPort)
{
	if (!PlayerController.IsValid() || !PlayerController->IsLocalController())
		return;
	if(Monitor->ResetCache)
		GeometryStreamingService.Reset();
	ENetAddress ListenAddress;
	ListenAddress.host = ENET_HOST_ANY;
	//enet_address_set_host(&ListenAddress, "192.168.3.6");
	ListenAddress.port = ListenPort;
	// ServerHost will live for the lifetime of the session.
	ServerHost = enet_host_create(&ListenAddress, 1, static_cast<enet_uint8>(avs::RemotePlaySessionChannel::RPCH_NumChannels), 0, 0);
	if (!ServerHost)
	{
		UE_LOG(LogRemotePlay, Error, TEXT("Session: Failed to create ENET server host"));
	}

	if (DiscoveryPort > 0)
	{
		if (!DiscoveryService.Initialize(Monitor,DiscoveryPort, ListenPort))
		{
			UE_LOG(LogRemotePlay, Warning, TEXT("Session: Failed to initialize discovery service"));
		}
	}
}

void URemotePlaySessionComponent::StopSession()
{
	ReleasePlayerPawn();
	DiscoveryService.Shutdown();

	if (ClientPeer)
	{
		check(ServerHost);

		enet_host_flush(ServerHost);
		enet_peer_disconnect(ClientPeer, 0);

		ENetEvent Event;
		bool bIsPeerConnected = true;
		while (bIsPeerConnected && enet_host_service(ServerHost, &Event, DisconnectTimeout) > 0)
		{
			switch (Event.type)
			{
			case ENET_EVENT_TYPE_RECEIVE:
				enet_packet_destroy(Event.packet);
				break;
			case ENET_EVENT_TYPE_DISCONNECT:
				bIsPeerConnected = false;
				break;
			}
		}
		if (bIsPeerConnected)
		{
			enet_peer_reset(ClientPeer);
		}
		ClientPeer = nullptr;
	}

	if (ServerHost)
	{
		enet_host_destroy(ServerHost);
		ServerHost = nullptr;
	}
}

void URemotePlaySessionComponent::SwitchPlayerPawn(APawn* NewPawn)
{
	check(ServerHost);
	check(ClientPeer);

	ReleasePlayerPawn();
	PlayerPawn = NewPawn;

	if (PlayerPawn.IsValid())
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

		StartStreaming();
	}
}
void URemotePlaySessionComponent::StartStreaming()
{
	URemotePlayCaptureComponent* CaptureComponent = Cast<URemotePlayCaptureComponent>(PlayerPawn->GetComponentByClass(URemotePlayCaptureComponent::StaticClass()));
	if (!CaptureComponent)
		return;
	
	delete RemotePlayContext;
	RemotePlayContext = new FRemotePlayContext;
	RemotePlayContext->ColorQueue.Reset(new avs::Queue);
	RemotePlayContext->ColorQueue->configure(16);

#if 0

	// Roderick: with consumer GPU's we can't have more than one video stream.
	// So we're encoding depth as alpha: no need for a separate source.
	if (CaptureComponent->CaptureSource == ESceneCaptureSource::SCS_SceneColorSceneDepth)
	{
		RemotePlayContext->bCaptureDepth = true;
		RemotePlayContext->DepthQueue.Reset(new avs::Queue);
		RemotePlayContext->DepthQueue->configure(16); 
	}
	else
#endif
	{
		RemotePlayContext->bCaptureDepth = false;
	}
	RemotePlayContext->GeometryQueue.Reset(new avs::Queue);
	RemotePlayContext->GeometryQueue->configure(16);

	const auto& EncodeParams = CaptureComponent->GetEncodeParams();
	const int32 StreamingPort = ServerHost->address.port + 1;
	avs::SetupCommand setupCommand;
	setupCommand.video_width	= EncodeParams.FrameWidth;
	setupCommand.video_height	= EncodeParams.FrameHeight;
	setupCommand.depth_height	= EncodeParams.DepthHeight;
	setupCommand.depth_width	= EncodeParams.DepthWidth;
	setupCommand.colour_cubemap_size = EncodeParams.FrameWidth / 3;
	setupCommand.compose_cube	= EncodeParams.bDecomposeCube;
	setupCommand.port = StreamingPort; 
	setupCommand.debug_stream=Monitor->DebugStream;
	setupCommand.do_checksums = Monitor->Checksums?1:0;
	setupCommand.server_id = Monitor->GetServerID();
	setupCommand.use_10_bit_decoding = Monitor->bUse10BitEncoding;
	setupCommand.use_yuv_444_decoding = Monitor->bUseYUV444Decoding;

	//Fill the list of streamed actors, so a reconnecting client will not have to download geometry it already has.
	TSet<AActor*> actorsOverlappingOnStart;
	DetectionSphereInner->GetOverlappingActors(actorsOverlappingOnStart);
	for(AActor* actor : actorsOverlappingOnStart)
	{
		GeometryStreamingService.AddActor(actor);
	}

	//Get resources the client will need to check it has.
	std::vector<avs::MeshNodeResources> outMeshResources;
	std::vector<avs::LightNodeResources> outLightResources;
	GeometryStreamingService.GetResourcesToStream(outMeshResources, outLightResources);

	std::vector<avs::uid> resourcesClientNeeds;
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

	setupCommand.resourceCount = resourcesClientNeeds.size();

	Client_SendCommand<avs::uid>(setupCommand, resourcesClientNeeds);

	//If the client needs a resource it will tell us; we don't want to stream the data if the client already has it.
	for(avs::uid resourceID : resourcesClientNeeds)
	{
		GeometryStreamingService.ConfirmResource(resourceID);
	}

	IsStreaming = true;
}

void URemotePlaySessionComponent::ReleasePlayerPawn()
{
	StopStreaming();
	if (PlayerPawn.IsValid())
	{
		PlayerPawn.Reset();
	}
}

void URemotePlaySessionComponent::StopStreaming()
{
	GeometryStreamingService.StopStreaming();
	if (ClientPeer)
	{
		Client_SendCommand(TEXT("v 0 0 0"));
	}
	if (RemotePlayContext)
	{
		if (RemotePlayContext->EncodePipeline.IsValid())
		{
			RemotePlayContext->EncodePipeline->Release();
			RemotePlayContext->EncodePipeline.Reset();
		}
		if (RemotePlayContext->NetworkPipeline.IsValid())
		{
			RemotePlayContext->NetworkPipeline->Release();
			RemotePlayContext->NetworkPipeline.Reset();
		}
		RemotePlayContext->ColorQueue.Reset();
		RemotePlayContext->DepthQueue.Reset();
		RemotePlayContext->GeometryQueue.Reset();
	}
	if (PlayerPawn.IsValid())
	{
		URemotePlayCaptureComponent* CaptureComponent = Cast<URemotePlayCaptureComponent>(PlayerPawn->GetComponentByClass(URemotePlayCaptureComponent::StaticClass()));
		if (CaptureComponent)
		{
			CaptureComponent->StopStreaming();
		}
	}
	delete RemotePlayContext;
	RemotePlayContext = nullptr;
	IsStreaming = false;
}

void URemotePlaySessionComponent::OnInnerSphereBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if(IsStreaming)
	{
		avs::uid actor_uid = GeometryStreamingService.AddActor(OtherActor);
		if(actor_uid != 0 && IsStreaming)
		{
			//Don't tell the client to show an actor it has yet to receive.
			if(!GeometryStreamingService.HasResource(actor_uid))
			{
				return;
			}

			ActorsEnteredBounds.push_back(actor_uid);
			ActorsLeftBounds.erase(std::remove(ActorsLeftBounds.begin(), ActorsLeftBounds.end(), actor_uid), ActorsLeftBounds.end());

			UE_LOG(LogRemotePlay, Verbose, TEXT("\"%s\" overlapped with actor \"%s\"."), *OverlappedComponent->GetName(), *OtherActor->GetName());
		}
		else
		{
			UE_LOG(LogRemotePlay, Warning, TEXT("Actor \"%s\" overlapped with \"%s\", but the actor is not supported! Only use supported component types, and check collision settings!"), *OverlappedComponent->GetName(), *OtherActor->GetName())
		}
	}
}

void URemotePlaySessionComponent::OnOuterSphereEndOverlap(UPrimitiveComponent * OverlappedComponent, AActor * OtherActor, UPrimitiveComponent * OtherComp, int32 OtherBodyIndex)
{
	if(IsStreaming)
	{
		avs::uid actor_uid = GeometryStreamingService.RemoveActor(OtherActor);
		if(actor_uid != 0)
		{
			ActorsLeftBounds.push_back(actor_uid);
			ActorsEnteredBounds.erase(std::remove(ActorsEnteredBounds.begin(), ActorsEnteredBounds.end(), actor_uid), ActorsEnteredBounds.end());

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
void URemotePlaySessionComponent::RecvHandshake(const ENetPacket* Packet)
{
	if (Packet->dataLength != sizeof(avs::Handshake))
	{
		UE_LOG(LogRemotePlay, Warning, TEXT("Session: Received malformed handshake packet of length: %d"), Packet->dataLength);
		return;
	}
	avs::Handshake handshake;
	FPlatformMemory::Memcpy(&handshake, Packet->data, Packet->dataLength);
	if (handshake.isReadyToReceivePayloads != true)
	{ 
		UE_LOG(LogRemotePlay, Warning, TEXT("Session: Handshake not ready to receive."));
		return;
	}

	if(handshake.usingHands)
	{
		GeometryStreamingService.AddControllersToStream();
	}

	RemotePlayContext->axesStandard = handshake.axesStandard;
	URemotePlayCaptureComponent* CaptureComponent = Cast<URemotePlayCaptureComponent>(PlayerPawn->GetComponentByClass(URemotePlayCaptureComponent::StaticClass()));
	const int32 StreamingPort = ServerHost->address.port + 1;

	if(!RemotePlayContext->NetworkPipeline.IsValid())
	{
		FRemotePlayNetworkParameters NetworkParams;
		NetworkParams.RemoteIP = Client_GetIPAddress();
		NetworkParams.LocalPort = StreamingPort;
		NetworkParams.RemotePort = NetworkParams.LocalPort + 1;
		NetworkParams.ClientBandwidthLimit = handshake.maxBandwidthKpS;
		NetworkParams.ClientBufferSize = handshake.udpBufferSize;

		RemotePlayContext->NetworkPipeline.Reset(new FNetworkPipeline);
		RemotePlayContext->NetworkPipeline->Initialize(Monitor, NetworkParams, RemotePlayContext->ColorQueue.Get(), RemotePlayContext->DepthQueue.Get(), RemotePlayContext->GeometryQueue.Get());
	}

	FCameraInfo& ClientCamInfo = CaptureComponent->GetClientCameraInfo();
	ClientCamInfo.FOV = handshake.FOV;
	ClientCamInfo.isVR = handshake.isVR;

	CaptureComponent->StartStreaming(RemotePlayContext);

	if (Monitor&&Monitor->StreamGeometry)
	{
		GeometryStreamingService.SetStreamingContinuously(Monitor->StreamGeometryContinuously);
		GeometryStreamingService.StartStreaming(RemotePlayContext);
	}
	avs::AcknowledgeHandshakeCommand ack;
	Client_SendCommand(ack);
	UE_LOG(LogRemotePlay, Log, TEXT("RemotePlay: Started streaming to %s:%d"), *Client_GetIPAddress(), StreamingPort);
}

void URemotePlaySessionComponent::DispatchEvent(const ENetEvent& Event)
{
	switch (Event.channelID)
	{
	case static_cast<enet_uint8>(avs::RemotePlaySessionChannel::RPCH_Handshake):
		//Delay the actual start of streaming until we receive a confirmation from the client that they are ready.
		RecvHandshake(Event.packet);
		break;
	case static_cast<enet_uint8>(avs::RemotePlaySessionChannel::RPCH_Control):
		RecvInput(Event.packet);
		break;
	case static_cast<enet_uint8>(avs::RemotePlaySessionChannel::RPCH_DisplayInfo):
		RecvDisplayInfo(Event.packet);
		break;
	case static_cast<enet_uint8>(avs::RemotePlaySessionChannel::RPCH_HeadPose):
		RecvHeadPose(Event.packet);
		break;
	case static_cast<enet_uint8>(avs::RemotePlaySessionChannel::RPCH_ResourceRequest):
	{
		size_t resourceAmount;
		memcpy(&resourceAmount, Event.packet->data, sizeof(size_t));

		std::vector<avs::uid> resourceRequests(resourceAmount);
		memcpy(resourceRequests.data(), Event.packet->data + sizeof(size_t), sizeof(avs::uid) * resourceAmount);

		for(avs::uid uid : resourceRequests)
		{
			GeometryStreamingService.RequestResource(uid);
		}
		break;
	}
	case static_cast<enet_uint8>(avs::RemotePlaySessionChannel::RPCH_KeyframeRequest):
		if(PlayerPawn.IsValid())
		{
			URemotePlayCaptureComponent* CaptureComponent = Cast<URemotePlayCaptureComponent>(PlayerPawn->GetComponentByClass(URemotePlayCaptureComponent::StaticClass()));
			if(CaptureComponent)
			{
				CaptureComponent->RequestKeyframe();
			}
		}
		else
		{
			UE_LOG(LogRemotePlay, Warning, TEXT("Received keyframe request; but player pawn, and thus the CaptureComponent, isn't set."))
		}

		break;
	case static_cast<enet_uint8>(avs::RemotePlaySessionChannel::RPCH_ClientMessage):
		RecvClientMessage(Event.packet);
		break;
	default:
		break;
	}
	enet_packet_destroy(Event.packet);
}

void URemotePlaySessionComponent::RecvDisplayInfo(const ENetPacket* Packet)
{
	struct DisplayInfo
	{
		uint32 Width;
		uint32 Height;
	};
	if (Packet->dataLength != sizeof(DisplayInfo))
	{
		UE_LOG(LogRemotePlay, Warning, TEXT("Session: Received malformed display info packet of length: %d"), Packet->dataLength);
		return;
	}
	if (!RemotePlayContext)
		return;

	DisplayInfo displayInfo;
	FPlatformMemory::Memcpy(&displayInfo, Packet->data, Packet->dataLength);

	URemotePlayCaptureComponent* CaptureComponent = Cast<URemotePlayCaptureComponent>(PlayerPawn->GetComponentByClass(URemotePlayCaptureComponent::StaticClass()));
	FCameraInfo& ClientCamInfo = CaptureComponent->GetClientCameraInfo();
	ClientCamInfo.Width = displayInfo.Width;
	ClientCamInfo.Height = displayInfo.Height;
}

void URemotePlaySessionComponent::RecvHeadPose(const ENetPacket* Packet)
{
	struct HeadPose
	{
		avs::vec4 OrientationQuat;
		avs::vec3 Position;
	};
	if (Packet->dataLength != sizeof(HeadPose))
	{
		UE_LOG(LogRemotePlay, Warning, TEXT("Session: Received malformed head pose packet of length: %d"), Packet->dataLength);
		return;
	}
	if (!RemotePlayContext)
		return;
	if (RemotePlayContext->axesStandard == avs::AxesStandard::NotInitialized)
		return;
	HeadPose headPose;
	FPlatformMemory::Memcpy(&headPose, Packet->data, Packet->dataLength);

	// Here we set the angle of the player pawn.
	// Convert quaternion from Simulcaster coordinate system (X right, Y forward, Z up) to UE4 coordinate system (left-handed, X left, Y forward, Z up).
	avs::ConvertRotation(RemotePlayContext->axesStandard,avs::AxesStandard::UnrealStyle, headPose.OrientationQuat);
	avs::ConvertPosition(RemotePlayContext->axesStandard, avs::AxesStandard::UnrealStyle, headPose.Position);
	FVector pos(headPose.Position.x, headPose.Position.y, headPose.Position.z);
	// back to centimetres...
	pos *= 100.0f;
	static FVector offset(0, 0, 1.5f);
	FVector oldPos = PlayerController->GetPawn()->GetActorLocation();
	FVector newPos = pos + offset;
	newPos.Z = oldPos.Z;
	PlayerController->GetPawn()->SetActorLocation(newPos);
	const FQuat HeadPoseUE{ headPose.OrientationQuat.x, headPose.OrientationQuat.y, headPose.OrientationQuat.z, headPose.OrientationQuat.w };
	
	URemotePlayCaptureComponent* CaptureComponent = Cast<URemotePlayCaptureComponent>(PlayerPawn->GetComponentByClass(URemotePlayCaptureComponent::StaticClass()));
	FCameraInfo& ClientCamInfo = CaptureComponent->GetClientCameraInfo();
	ClientCamInfo.Orientation = HeadPoseUE;
	ClientCamInfo.Position = pos;

	FVector Euler = HeadPoseUE.Euler();
	Euler.X = Euler.Y = 0.0f;
	// Unreal thinks the Euler angle starts from facing X, but actually it's Y.
	//Euler.Z += 180.0f;
	FQuat FlatPose = FQuat::MakeFromEuler(Euler);
	check(PlayerController.IsValid());
	PlayerController->SetControlRotation(FlatPose.Rotator());
}

void URemotePlaySessionComponent::RecvClientMessage(const ENetPacket* packet)
{
	avs::ClientMessagePayloadType clientMessagePayloadType = *((avs::ClientMessagePayloadType*)packet->data);
	switch (clientMessagePayloadType)
	{
		case avs::ClientMessagePayloadType::ActorStatus:
		{
			size_t messageSize = sizeof(avs::ActorStatusMessage);
			avs::ActorStatusMessage message;
			memcpy(&message, packet->data, messageSize);

			size_t drawnSize = sizeof(avs::uid) * message.actorsDrawnAmount;
			std::vector<avs::uid> drawn(message.actorsDrawnAmount);
			memcpy(drawn.data(), packet->data + messageSize, drawnSize);

			size_t toReleaseSize = sizeof(avs::uid) * message.actorsWantToReleaseAmount;
			std::vector<avs::uid> toRelease(message.actorsWantToReleaseAmount);
			memcpy(toRelease.data(), packet->data + messageSize + drawnSize, toReleaseSize);

			for(avs::uid actor_uid : drawn)
			{
				GeometryStreamingService.HideActor(actor_uid);
			}

			for(avs::uid actor_uid : toRelease)
			{
				GeometryStreamingService.ShowActor(actor_uid);
			}
		}
		case avs::ClientMessagePayloadType::ReceivedResources:
		{
			size_t messageSize = sizeof(avs::ReceivedResourcesMessage);
			avs::ReceivedResourcesMessage message;
			memcpy(&message, packet->data, messageSize);

			size_t confirmedResourcesSize = sizeof(avs::uid) * message.receivedResourcesAmount;
			std::vector<avs::uid> confirmedResources(message.receivedResourcesAmount);
			memcpy(confirmedResources.data(), packet->data + messageSize, confirmedResourcesSize);

			for(avs::uid uid : confirmedResources)
			{
				GeometryStreamingService.ConfirmResource(uid);
			}

			break;
		}
		break;
		default:
			break;
	};
}

void URemotePlaySessionComponent::RecvInput(const ENetPacket* Packet)
{
	struct FInputState
	{
		uint32 ButtonsPressed;
		uint32 ButtonsReleased;
		float RelativeTouchX;
		float RelativeTouchY;
		float JoystickX;
		float JoystickY;
	};
	FInputState InputState;

	if (Packet->dataLength != sizeof(FInputState))
	{
		UE_LOG(LogRemotePlay, Warning, TEXT("Session: Received malfored input state change packet of length: %d"), Packet->dataLength);
		return;
	} 

	FPlatformMemory::Memcpy(&InputState, Packet->data, Packet->dataLength);
	InputTouchAxis.X = FMath::Clamp(InputState.RelativeTouchX * InputTouchSensitivity, -1.0f, 1.0f);
	InputTouchAxis.Y = FMath::Clamp(InputState.RelativeTouchY * InputTouchSensitivity, -1.0f, 1.0f);
	InputJoystick.X = InputState.JoystickX;
	InputJoystick.Y = InputState.JoystickY;
	TranslateButtons(InputState.ButtonsPressed, InputQueue.ButtonsPressed);
	TranslateButtons(InputState.ButtonsReleased, InputQueue.ButtonsReleased);
}

inline bool URemotePlaySessionComponent::Client_SendCommand(const FString& Cmd) const
{
	check(ClientPeer);
	// Convert the string to UTF8:
	TStringConversion<FTCHARToUTF8_Convert> utf8((const TCHAR*)(*Cmd));
	// first we send a payload type id. We here send the id that means "this is a text packet".
	std::vector<uint8_t> packet_buffer;
	avs::TextCommand textCommand;
	size_t cmdSize = avs::GetCommandSize(textCommand.commandPayloadType);
	packet_buffer.resize(cmdSize+utf8.Length()+1);
	memcpy(packet_buffer.data() + cmdSize, utf8.Get(), utf8.Length()+1);// 1 extra char, should be zero!
	check(packet_buffer[packet_buffer.size() - 1] == (uint8_t)0);
	memcpy(packet_buffer.data(), &textCommand.commandPayloadType, cmdSize);
	ENetPacket* Packet = enet_packet_create(packet_buffer.data(),packet_buffer.size(), ENET_PACKET_FLAG_RELIABLE);
	check(Packet);
	return enet_peer_send(ClientPeer, static_cast<enet_uint8>(avs::RemotePlaySessionChannel::RPCH_Control), Packet) == 0;
}

bool URemotePlaySessionComponent::Client_SendCommand(const avs::Command &avsCommand) const
{
	check(ClientPeer);
	size_t commandSize = avs::GetCommandSize(avsCommand.commandPayloadType);
	ENetPacket* Packet = enet_packet_create(&avsCommand, commandSize, ENET_PACKET_FLAG_RELIABLE);
	check(Packet);
	return enet_peer_send(ClientPeer, static_cast<enet_uint8>(avs::RemotePlaySessionChannel::RPCH_Control), Packet) == 0;
}

template<typename T>
bool URemotePlaySessionComponent::Client_SendCommand(const avs::Command &avsCommand, std::vector<T>& appendedList) const
{
	check(ClientPeer);
	size_t commandSize = avs::GetCommandSize(avsCommand.commandPayloadType);
	size_t listSize = sizeof(T) * appendedList.size();

	ENetPacket* Packet = enet_packet_create(&avsCommand, commandSize , ENET_PACKET_FLAG_RELIABLE);
	enet_packet_resize(Packet, commandSize + listSize);
	
	//Copy list into packet.
	//enet_packet_resize(Packet, commandSize + listSize);
	memcpy(Packet->data + commandSize, appendedList.data(), listSize);

	check(Packet);
	return enet_peer_send(ClientPeer, static_cast<enet_uint8>(avs::RemotePlaySessionChannel::RPCH_Control), Packet) == 0;
}

inline FString URemotePlaySessionComponent::Client_GetIPAddress() const
{
	check(ClientPeer);

	char IPAddr[20];
	enet_address_get_host_ip(&ClientPeer->address, IPAddr, sizeof(IPAddr));
	return FString(ANSI_TO_TCHAR(IPAddr));
}

inline uint16 URemotePlaySessionComponent::Client_GetPort() const
{
	check(ClientPeer);
	return ClientPeer->address.port;
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
