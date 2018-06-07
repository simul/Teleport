// Copyright 2018 Simul.co

#include "RemotePlaySessionComponent.h"
#include "RemotePlayCaptureComponent.h"
#include "RemotePlayModule.h"

#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

#include "enet/enet.h"

enum ERemotePlaySessionChannel
{
	RPCH_Command  = 0,
	RPCH_Control  = 1,
	RPCH_HeadPose = 2,
	RPCH_NumChannels,
};

URemotePlaySessionComponent::URemotePlaySessionComponent()
	: ServerHost(nullptr)
	, ClientPeer(nullptr)
	, bAutoStartSession(true)
	, AutoListenPort(10500)
	, DisconnectTimeout(1000)
{
	PrimaryComponentTick.bCanEverTick = true;
}
	
void URemotePlaySessionComponent::BeginPlay()
{
	Super::BeginPlay();

	PlayerController = Cast<APlayerController>(GetOuter());
	if(!PlayerController.IsValid())
	{
		UE_LOG(LogRemotePlay, Error, TEXT("Session: Session component must be attached to a player controller!"));
		return;
	}

	if(bAutoStartSession)
	{
		StartSession(AutoListenPort);
	}
}
	
void URemotePlaySessionComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	StopSession();
	Super::EndPlay(Reason);
}
	
void URemotePlaySessionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	if(!HasBegunPlay())
	{
		return;
	}
	if(!ServerHost || !PlayerController.IsValid())
	{
		return;
	}

	if(ClientPeer && PlayerPawn != PlayerController->GetPawn())
	{
		SwitchPlayerPawn(PlayerController->GetPawn());
	}

	ENetEvent Event;
	while(enet_host_service(ServerHost, &Event, 0) > 0)
	{
		switch(Event.type)
		{
		case ENET_EVENT_TYPE_CONNECT:
			check(ClientPeer == nullptr);
			ClientPeer = Event.peer;
			UE_LOG(LogRemotePlay, Log, TEXT("Client connected: %s:%d"), *Client_GetIPAddress(), Client_GetPort());
			break;
		case ENET_EVENT_TYPE_DISCONNECT:
			check(ClientPeer == Event.peer);
			UE_LOG(LogRemotePlay, Log, TEXT("Client disconnected: %s:%d"), *Client_GetIPAddress(), Client_GetPort());
			ReleasePlayerPawn();
			ClientPeer = nullptr;
			break;
		case ENET_EVENT_TYPE_RECEIVE:
			DispatchEvent(Event);
			break;
		}
	}
}

void URemotePlaySessionComponent::StartSession(int32 ListenPort)
{
	if(PlayerController.IsValid() && PlayerController->IsLocalController())
	{
		ENetAddress ListenAddress;
		ListenAddress.host = ENET_HOST_ANY;
		ListenAddress.port = ListenPort;

		ServerHost = enet_host_create(&ListenAddress, 1, RPCH_NumChannels, 0, 0);
		if(!ServerHost)
		{
			UE_LOG(LogRemotePlay, Error, TEXT("Session: Failed to create ENET server host"));
		}
	}
}

void URemotePlaySessionComponent::StopSession()
{
	ReleasePlayerPawn();

	if(ClientPeer)
	{
		check(ServerHost);
		
		enet_host_flush(ServerHost);
		enet_peer_disconnect(ClientPeer, 0);

		ENetEvent Event;
		bool bIsPeerConnected = true;
		while(bIsPeerConnected && enet_host_service(ServerHost, &Event, DisconnectTimeout) > 0)
		{
			switch(Event.type)
			{
			case ENET_EVENT_TYPE_RECEIVE:
				enet_packet_destroy(Event.packet);
				break;
			case ENET_EVENT_TYPE_DISCONNECT:
				bIsPeerConnected = false;
				break;
			}
		}
		if(bIsPeerConnected)
		{
			enet_peer_reset(ClientPeer);
		}
		ClientPeer = nullptr;
	}

	if(ServerHost)
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

	if(PlayerPawn.IsValid())
	{
		URemotePlayCaptureComponent* CaptureComponent = Cast<URemotePlayCaptureComponent>(PlayerPawn->GetComponentByClass(URemotePlayCaptureComponent::StaticClass()));
		if(CaptureComponent)
		{
			const auto& EncodeParams  = CaptureComponent->EncodeParams;
			const int32 StreamingPort = ServerHost->address.port + 1;
			Client_SendCommand(FString::Printf(TEXT("v %d %d %d"), StreamingPort, EncodeParams.FrameWidth, EncodeParams.FrameHeight));
			CaptureComponent->StartStreaming(Client_GetIPAddress(), StreamingPort);
		}
	}
}

void URemotePlaySessionComponent::ReleasePlayerPawn()
{
	if(PlayerPawn.IsValid())
	{
		URemotePlayCaptureComponent* CaptureComponent = Cast<URemotePlayCaptureComponent>(PlayerPawn->GetComponentByClass(URemotePlayCaptureComponent::StaticClass()));
		if(CaptureComponent)
		{
			if(ClientPeer)
			{
				Client_SendCommand(TEXT("v 0 0 0"));
			}
			CaptureComponent->StopStreaming();
		}
		PlayerPawn.Reset();
	}
}
	
void URemotePlaySessionComponent::DispatchEvent(const ENetEvent& Event)
{
	switch(Event.channelID)
	{
	case RPCH_Control:
		// TODO: Implement.
		break;
	case RPCH_HeadPose:
		ParseHeadPose(Event.packet);
		break;
	}
	enet_packet_destroy(Event.packet);
}

void URemotePlaySessionComponent::ParseHeadPose(const ENetPacket* Packet)
{
	check(PlayerController.IsValid());

	if(Packet->dataLength != sizeof(FQuat))
	{
		UE_LOG(LogRemotePlay, Warning, TEXT("Session: Received malformed head pose packet of length: %d"), Packet->dataLength);
		return;
	}

	FQuat HeadPoseOVR;
	FPlatformMemory::Memcpy(&HeadPoseOVR, Packet->data, Packet->dataLength);

	// Convert quaternion from OVR (OpenGL) coordinate system to UE4 coordinate system.
	const FQuat HeadPose{ HeadPoseOVR.Z, -HeadPoseOVR.X, -HeadPoseOVR.Y, HeadPoseOVR.W };

	PlayerController->SetControlRotation(HeadPose.Rotator());
}
	
inline bool URemotePlaySessionComponent::Client_SendCommand(const FString& Cmd) const
{
	check(ClientPeer);
	ENetPacket* Packet = enet_packet_create(TCHAR_TO_UTF8(*Cmd), Cmd.Len(), ENET_PACKET_FLAG_RELIABLE);
	return enet_peer_send(ClientPeer, RPCH_Command, Packet) == 0;
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
