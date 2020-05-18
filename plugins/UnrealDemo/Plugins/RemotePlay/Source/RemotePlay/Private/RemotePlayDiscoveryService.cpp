// Copyright 2018 Simul.co
#define WIN32_LEAN_AND_MEAN

#include "RemotePlayDiscoveryService.h"
#include "RemotePlayModule.h"

#include "Common/UdpSocketBuilder.h"
#include "Serialization/BufferArchive.h"
#include "RemotePlaySettings.h"
#include "RemotePlayMonitor.h"
#include "Engine/World.h"

#include "SimulCasterServer/CasterSettings.h"

FRemotePlayDiscoveryService::FRemotePlayDiscoveryService(const SCServer::CasterSettings* settings)
	:casterSettings(settings)
{
	SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	check(SocketSubsystem);
}

bool FRemotePlayDiscoveryService::initialise(uint16_t inDiscoveryPort, uint16_t inServicePort)
{
	if(!inDiscoveryPort)
		inDiscoveryPort = LastDiscoveryPort;
	if(!inDiscoveryPort)
	{
		UE_LOG(LogRemotePlay, Error, TEXT("Discovery: No useable Discovery Port"));
		return false;
	}
	LastDiscoveryPort = inDiscoveryPort;

	if (!inServicePort) inServicePort = ServicePort;
	if(!inServicePort)
	{
		UE_LOG(LogRemotePlay, Error, TEXT("Discovery: No useable Service Port"));
		return false;
	}

	FIPv4Address boundAddr = FIPv4Address::Any;		// i.e. 127.0.0.1, 192.168.3.X (the server's local addr), or the server's global IP address.
	Socket = TUniquePtr<FSocket>(FUdpSocketBuilder(TEXT("RemotePlayDiscoveryService"))
								 .AsReusable()
								 .AsNonBlocking()
								 .BoundToAddress(boundAddr)
								 .BoundToPort(inDiscoveryPort)
								 .Build());

	if(!Socket.IsValid())
	{
		UE_LOG(LogRemotePlay, Error, TEXT("Discovery: Failed to bind to UDP port %d"), inDiscoveryPort);
		return false;
	}

	ServicePort = inServicePort;
	return true;
}

void FRemotePlayDiscoveryService::shutdown()
{
	Socket.Reset();
	Clients.Empty();
}

void FRemotePlayDiscoveryService::tick()
{
	if(!Socket.IsValid())
	{
		return;
	}
	uint32 Address = 0;

	TSharedRef<FInternetAddr> RecvAddr = SocketSubsystem->CreateInternetAddr();
	TSharedRef<FInternetAddr> ForcedAddr = SocketSubsystem->CreateInternetAddr();
	//UE_LOG(LogRemotePlay, Warning, TEXT("Socket bound to %s"), *RecvAddr->ToString(false));

	bool bIsValid = true;
	uint32 ip_forced = 0;
	if(wcslen(casterSettings->clientIP) != 0)
	{
		ForcedAddr->SetIp(casterSettings->clientIP, bIsValid);
		if(!bIsValid)
		{
			ForcedAddr->SetAnyAddress();
		}
		else
			ForcedAddr->GetIp(ip_forced);
	}
	uint32_t ClientID;
	int32_t BytesRead;
	while(Socket->RecvFrom(reinterpret_cast<uint8*>(&ClientID), sizeof(ClientID), BytesRead, *RecvAddr) && BytesRead == sizeof(ClientID))
	{
		uint32 ip_recv;
		RecvAddr->GetIp(ip_recv);
		if(ip_forced != 0 && ip_recv != ip_forced)
		{
			//UE_LOG(LogRemotePlay, Warning, TEXT("Mismatched Client found at %s"), *RecvAddr->ToString(false) );
			continue;
		}
		//UE_LOG(LogRemotePlay, Warning, TEXT("Matched Client found at %s"), *RecvAddr->ToString(false));
		Clients.AddUnique({RecvAddr, ClientID});
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
		else
		{
			LastFoundClientID=Client.ID;
		}
	}
}
