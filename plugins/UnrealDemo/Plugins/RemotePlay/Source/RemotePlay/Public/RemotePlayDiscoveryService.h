// Copyright 2018 Simul.co

#pragma once

#include "CoreMinimal.h"
#include "Sockets.h"
#include "IPAddress.h"

#include "SimulCasterServer/DiscoveryService.h"

namespace SCServer
{
	struct CasterSettings;
}

class FRemotePlayDiscoveryService : public SCServer::DiscoveryService
{
public:
	FRemotePlayDiscoveryService(const SCServer::CasterSettings* settings);
	virtual ~FRemotePlayDiscoveryService() = default;

	virtual bool initialise(uint16_t inDiscoveryPort = 0, uint16_t inServicePort = 0) override;
	virtual void shutdown() override;
	virtual void tick() override;
	virtual uint64_t getNewClientID()
	{
		return LastFoundClientID;
		LastFoundClientID=0;
	}

private:
	avs::uid LastFoundClientID;
	class ISocketSubsystem* SocketSubsystem;
	const SCServer::CasterSettings* casterSettings;

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
