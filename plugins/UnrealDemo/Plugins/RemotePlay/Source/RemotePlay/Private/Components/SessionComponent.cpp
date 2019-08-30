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
		GeometryStreamingService.Tick();
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
	GeometryStreamingService.Reset();

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

	const auto& EncodeParams = CaptureComponent->EncodeParams;
	const int32 StreamingPort = ServerHost->address.port + 1;
	avs::SetupCommand setupCommand;
	setupCommand.video_width = EncodeParams.FrameWidth;
	setupCommand.video_height = EncodeParams.FrameHeight;
	setupCommand.depth_height = EncodeParams.DepthHeight;
	setupCommand.depth_width = EncodeParams.DepthWidth;
	setupCommand.port = StreamingPort;
	Client_SendCommand(setupCommand);
	//Client_SendCommand(FString::Printf(TEXT("v %d %d %d"), StreamingPort, EncodeParams.FrameWidth, EncodeParams.FrameHeight));
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
		//Delay the actual start of streaming until we receive a confirmation from the client that they are ready.
	{
		URemotePlayCaptureComponent* CaptureComponent = Cast<URemotePlayCaptureComponent>(PlayerPawn->GetComponentByClass(URemotePlayCaptureComponent::StaticClass()));
		const int32 StreamingPort = ServerHost->address.port + 1;

		CaptureComponent->StartStreaming(RemotePlayContext);
		Monitor=ARemotePlayMonitor::Instantiate(GetWorld());
		if(Monitor&&Monitor->StreamGeometry)
		{
			GeometryStreamingService.SetStreamingContinuously(Monitor->StreamGeometryContinuously);
			GeometryStreamingService.StartStreaming(RemotePlayContext);
		}

		if(!RemotePlayContext->NetworkPipeline.IsValid())
		{
			FRemotePlayNetworkParameters NetworkParams;
			NetworkParams.RemoteIP = Client_GetIPAddress();
			NetworkParams.LocalPort = StreamingPort;
			NetworkParams.RemotePort = NetworkParams.LocalPort + 1;

			RemotePlayContext->NetworkPipeline.Reset(new FNetworkPipeline);
			RemotePlayContext->NetworkPipeline->Initialize(Monitor,NetworkParams, RemotePlayContext->ColorQueue.Get(), RemotePlayContext->DepthQueue.Get(), RemotePlayContext->GeometryQueue.Get());
		}

		UE_LOG(LogRemotePlay, Log, TEXT("RemotePlay: Started streaming to %s:%d"), *Client_GetIPAddress(), StreamingPort);
		break;
	}
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
	return enet_peer_send(ClientPeer, RPCH_Control, Packet) == 0;
}

bool URemotePlaySessionComponent::Client_SendCommand(const avs::Command &avsCommand) const
{
	check(ClientPeer);
	ENetPacket* Packet = enet_packet_create(&avsCommand, avs::GetCommandSize(avsCommand.commandPayloadType), ENET_PACKET_FLAG_RELIABLE);
	check(Packet);
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
