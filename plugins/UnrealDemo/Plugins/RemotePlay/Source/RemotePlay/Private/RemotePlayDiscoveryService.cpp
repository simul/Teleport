// Copyright 2018 Simul.co
#define WIN32_LEAN_AND_MEAN

#include "RemotePlayDiscoveryService.h"
#include "RemotePlayModule.h"

#include "Common/UdpSocketBuilder.h"
#include "Serialization/BufferArchive.h"
	
FRemotePlayDiscoveryService::FRemotePlayDiscoveryService()
{
	SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	check(SocketSubsystem);
}
	
bool FRemotePlayDiscoveryService::Initialize(uint16 InDiscoveryPort, uint16 InServicePort)
{
	if (!InDiscoveryPort)
		InDiscoveryPort = LastDiscoveryPort;
	if (!InDiscoveryPort)
	{
		UE_LOG(LogRemotePlay, Error, TEXT("Discovery: No useable Discovery Port"));
		return false;
	}
	LastDiscoveryPort = InDiscoveryPort;
	if (!InServicePort)
		InServicePort = ServicePort;
	if (!InServicePort)
	{
		UE_LOG(LogRemotePlay, Error, TEXT("Discovery: No useable Service Port"));
		return false;
	}
	Socket = TUniquePtr<FSocket>(FUdpSocketBuilder(TEXT("RemotePlayDiscoveryService"))
		.AsReusable()
		.AsNonBlocking()
		.BoundToAddress(FIPv4Address::Any)
		.BoundToPort(InDiscoveryPort)
		.Build());

	if(!Socket.IsValid())
	{
		UE_LOG(LogRemotePlay, Error, TEXT("Discovery: Failed to bind to UDP port %d"), InDiscoveryPort);
		return false;
	}

	ServicePort = InServicePort;
	return true;
}
	
void FRemotePlayDiscoveryService::Shutdown()
{
	Socket.Reset();
	Clients.Empty();
}
	
void FRemotePlayDiscoveryService::Tick()
{
	if(!Socket.IsValid())
	{
		return;
	}

	TSharedRef<FInternetAddr> RecvAddr = SocketSubsystem->CreateInternetAddr();
	
	uint32_t ClientID;
	int32_t BytesRead;
	while(Socket->RecvFrom(reinterpret_cast<uint8*>(&ClientID), sizeof(ClientID), BytesRead, *RecvAddr) && BytesRead == sizeof(ClientID))
	{
		Clients.AddUnique({ RecvAddr, ClientID });
	}

	for(FClient& Client : Clients)
	{
		FBufferArchive Ar;
		Ar << Client.ID;
		Ar << ServicePort;

		int32 BytesSend;
		const bool bSendResult = Socket->SendTo(Ar.GetData(), Ar.Num(), BytesSend, *Client.Addr);
		if(!bSendResult || BytesSend != Ar.Num())
		{
			UE_LOG(LogRemotePlay, Warning, TEXT("Discovery: Failed to send reply to client ID: %x"), Client.ID);
		}
	}
}
