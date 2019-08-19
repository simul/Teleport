// Copyright 2018 Simul.co
#define WIN32_LEAN_AND_MEAN

#include "Components/SessionComponent.h"
#include "Components/CaptureComponent.h"
#include "Components/StreamableGeometryComponent.h"
#include "RemotePlayModule.h"
#include "RemotePlaySettings.h"

#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
 
#include "enet/enet.h"
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

enum ERemotePlaySessionChannel
{
	RPCH_HANDSHAKE = 0,
	RPCH_Control = 1,
	RPCH_HeadPose = 2,
	RPCH_NumChannels,
};

URemotePlaySessionComponent::URemotePlaySessionComponent()
	: bAutoStartSession(true)
	, AutoListenPort(10500)
	, AutoDiscoveryPort(10607)
	, DisconnectTimeout(1000)
	, InputTouchSensitivity(1.0f)
	, InputTouchAxis(0.f, 0.f)
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

	Bandwidth = 0.0f;
	//INC_DWORD_STAT(STAT_BANDWIDTH); //Increments the counter by one each call.
#if STATS
	FString BandwidthName = GetName() + " Bandwidth kps";
	BandwidthStatID = CreateStatId<FStatGroup_STATGROUP_RemotePlay>(BandwidthName, EStatDataType::ST_double);
#endif // ENABLE_STATNAMEDEVENTS

	PlayerController = Cast<APlayerController>(GetOuter());
	if (!PlayerController.IsValid())
	{
		UE_LOG(LogRemotePlay, Error, TEXT("Session: Session component must be attached to a player controller!"));
		return;
	}

	if (bAutoStartSession)
	{
		StartSession(AutoListenPort, AutoDiscoveryPort);
	}
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

		//Only send the payloads if the client is ready to receive the payloads.
		if(isReadyForPayloads)
		{
			GeometryStreamingService.Tick();
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
			DiscoveryService.Initialize();
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
	ENetAddress ListenAddress;
	ListenAddress.host = ENET_HOST_ANY;
	//enet_address_set_host(&ListenAddress, "192.168.3.6");
	ListenAddress.port = ListenPort;
	// ServerHost will live for the lifetime of the session.
	ServerHost = enet_host_create(&ListenAddress, 1, RPCH_NumChannels, 0, 0);
	if (!ServerHost)
	{
		UE_LOG(LogRemotePlay, Error, TEXT("Session: Failed to create ENET server host"));
	}

	if (DiscoveryPort > 0)
	{
		if (!DiscoveryService.Initialize(DiscoveryPort, ListenPort))
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

	if (CaptureComponent->CaptureSource == ESceneCaptureSource::SCS_SceneColorSceneDepth)
	{
		RemotePlayContext->bCaptureDepth = true;
		RemotePlayContext->DepthQueue.Reset(new avs::Queue);
		RemotePlayContext->DepthQueue->configure(16); 
	}
	else
	{
		RemotePlayContext->bCaptureDepth = false;
	}
	RemotePlayContext->GeometryQueue.Reset(new avs::Queue);
	RemotePlayContext->GeometryQueue->configure(16);

	const auto& EncodeParams = CaptureComponent->EncodeParams;
	const int32 StreamingPort = ServerHost->address.port + 1;
	Client_SendCommand(FString::Printf(TEXT("v %d %d %d"), StreamingPort, EncodeParams.FrameWidth, EncodeParams.FrameHeight));
	
	CaptureComponent->StartStreaming(RemotePlayContext);
	const URemotePlaySettings *RemotePlaySettings = GetDefault<URemotePlaySettings>();
	if (RemotePlaySettings&&RemotePlaySettings->StreamGeometry)
	{
		GeometryStreamingService.StartStreaming(GetWorld(), IRemotePlay::Get().GetGeometrySource(), RemotePlayContext);
	}

	if (!RemotePlayContext->NetworkPipeline.IsValid())
	{
		FRemotePlayNetworkParameters NetworkParams;
		NetworkParams.RemoteIP = Client_GetIPAddress();
		NetworkParams.LocalPort = StreamingPort;
		NetworkParams.RemotePort = NetworkParams.LocalPort + 1;

		RemotePlayContext->NetworkPipeline.Reset(new FNetworkPipeline);
		RemotePlayContext->NetworkPipeline->Initialize(NetworkParams, RemotePlayContext->ColorQueue.Get(), RemotePlayContext->DepthQueue.Get(), RemotePlayContext->GeometryQueue.Get());
	}

	UE_LOG(LogRemotePlay, Log, TEXT("RemotePlay: Started streaming to %s:%d"), *Client_GetIPAddress(), StreamingPort);
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
}

void URemotePlaySessionComponent::ApplyPlayerInput(float DeltaTime)
{
	check(PlayerController.IsValid());
	PlayerController->InputAxis(EKeys::MotionController_Right_Thumbstick_X, InputTouchAxis.X, DeltaTime, 1, true);
	PlayerController->InputAxis(EKeys::MotionController_Right_Thumbstick_Y, InputTouchAxis.Y, DeltaTime, 1, true);

	while (InputQueue.ButtonsPressed.Num() > 0)
	{
		PlayerController->InputKey(InputQueue.ButtonsPressed.Pop(), EInputEvent::IE_Pressed, 1.0f, true);
	}
	while (InputQueue.ButtonsReleased.Num() > 0)
	{
		PlayerController->InputKey(InputQueue.ButtonsReleased.Pop(), EInputEvent::IE_Released, 1.0f, true);
	}
}

void URemotePlaySessionComponent::DispatchEvent(const ENetEvent& Event)
{
	switch (Event.channelID)
	{
	case RPCH_HANDSHAKE:
		memcpy(&isReadyForPayloads, Event.packet->data, Event.packet->dataLength);
		break;
	case RPCH_Control:
		RecvInput(Event.packet);
		break;
	case RPCH_HeadPose:
		RecvHeadPose(Event.packet);
		break;
	}
	enet_packet_destroy(Event.packet);
}

void URemotePlaySessionComponent::RecvHeadPose(const ENetPacket* Packet)
{
	if (Packet->dataLength != sizeof(FQuat))
	{
		UE_LOG(LogRemotePlay, Warning, TEXT("Session: Received malformed head pose packet of length: %d"), Packet->dataLength);
		return;
	}

	FQuat HeadPose;
	FPlatformMemory::Memcpy(&HeadPose, Packet->data, Packet->dataLength);

	// Here we set the angle of the player pawn.
	// Convert quaternion from Simulcaster coordinate system (X right, Y forward, Z up) to UE4 coordinate system (left-handed, X left, Y forward, Z up).
	const FQuat HeadPoseUE{ -HeadPose.X, HeadPose.Y, -HeadPose.Z, HeadPose.W };
	FVector Euler = HeadPoseUE.Euler();
	Euler.X = Euler.Y = 0.0f;
	// Unreal thinks the Euler angle starts from facing X, but actually it's Y.
	Euler.Z -= 90.0f;
	FQuat FlatPose = FQuat::MakeFromEuler(Euler);
	check(PlayerController.IsValid());
	PlayerController->SetControlRotation(FlatPose.Rotator());
}

void URemotePlaySessionComponent::RecvInput(const ENetPacket* Packet)
{
	struct FInputState
	{
		uint32 ButtonsPressed;
		uint32 ButtonsReleased;
		float RelativeTouchX;
		float RelativeTouchY;
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
	TranslateButtons(InputState.ButtonsPressed, InputQueue.ButtonsPressed);
	TranslateButtons(InputState.ButtonsReleased, InputQueue.ButtonsReleased);
}

inline bool URemotePlaySessionComponent::Client_SendCommand(const FString& Cmd) const
{
	check(ClientPeer);
	ENetPacket* Packet = enet_packet_create(TCHAR_TO_UTF8(*Cmd), Cmd.Len(), ENET_PACKET_FLAG_RELIABLE);
	return enet_peer_send(ClientPeer, RPCH_Control, Packet) == 0;
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
