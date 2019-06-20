// Copyright 2018 Simul.co

#pragma once

#include "CoreMinimal.h"
#include "Sockets.h"
#include "IPAddress.h"

class FRemotePlayDiscoveryService
{
public:
	FRemotePlayDiscoveryService();

	bool Initialize(uint16 InDiscoveryPort=0, uint16 InServicePort=0);
	void Shutdown();
	void Tick();

private:
	class ISocketSubsystem* SocketSubsystem;

	TUniquePtr<FSocket> Socket;
	uint16 ServicePort;
	uint16 LastDiscoveryPort = 0;
	struct FClient
	{
		TSharedRef<FInternetAddr> Addr;
		uint32_t ID;

		bool operator==(const FClient& Other) const
		{
			return *Addr == *Other.Addr && ID == Other.ID;
		}
	};
	TArray<FClient> Clients;
};
