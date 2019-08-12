// Copyright 2018 Simul.co
#define WIN32_LEAN_AND_MEAN

#include "RemotePlayDiscoveryService.h"
#include "RemotePlayModule.h"

#include "Common/UdpSocketBuilder.h"
#include "Serialization/BufferArchive.h"
#include "RemotePlaySettings.h"
	
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
	const URemotePlaySettings *RemotePlaySettings = GetDefault<URemotePlaySettings>();
	FIPv4Address boundAddr = FIPv4Address::Any;		// i.e. 127.0.0.1, 192.168.3.X (the server's local addr), or the server's global IP address.
	//FIPv4Address::Parse(FString("192.168.3.6"), boundAddr);
	Socket = TUniquePtr<FSocket>(FUdpSocketBuilder(TEXT("RemotePlayDiscoveryService"))
		.AsReusable()
		.AsNonBlocking()
		.BoundToAddress(boundAddr)
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
	uint32 Address = 0;

	TSharedRef<FInternetAddr> RecvAddr = SocketSubsystem->CreateInternetAddr();
	TSharedRef<FInternetAddr> ForcedAddr = SocketSubsystem->CreateInternetAddr();
	//UE_LOG(LogRemotePlay, Warning, TEXT("Socket bound to %s"), *RecvAddr->ToString(false));

	const URemotePlaySettings *RemotePlaySettings = GetDefault<URemotePlaySettings>();
	bool bIsValid = true;
	uint32 ip_forced = 0; 
	if (RemotePlaySettings->ClientIP.Len()) 
	{ 
		ForcedAddr->SetIp(*RemotePlaySettings->ClientIP, bIsValid);
		if (!bIsValid)
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
		if (ip_forced !=0 && ip_recv!=ip_forced)
		{
			//UE_LOG(LogRemotePlay, Warning, TEXT("Mismatched Client found at %s"), *RecvAddr->ToString(false) );
			continue;
		}
		//UE_LOG(LogRemotePlay, Warning, TEXT("Matched Client found at %s"), *RecvAddr->ToString(false));
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
