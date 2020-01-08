// Copyright 2018 Simul.co

#pragma once

#include "SimulCasterServer/NetworkPipeline.h"

#include "CoreMinimal.h"
#include "RemotePlayParameters.h"

#if !UE_BUILD_SHIPPING || !UE_BUILD_TEST
#define WITH_REMOTEPLAY_STATS 1
#endif

class FNetworkPipeline: public SCServer::NetworkPipeline
{
public:
	FNetworkPipeline() = default;

	void initialise(class ARemotePlayMonitor* m, const FRemotePlayNetworkParameters& InParams, avs::Queue* ColorQueue, avs::Queue* DepthQueue, avs::Queue* GeometryQueue);
	void release() override;
	void process() override;
private:
	class ARemotePlayMonitor* Monitor;

#if WITH_REMOTEPLAY_STATS
	double LastTimestamp = 0.0;
#endif // WITH_REMOTEPLAY_STATS
};