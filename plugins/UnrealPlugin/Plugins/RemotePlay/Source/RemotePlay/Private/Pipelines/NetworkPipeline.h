// Copyright 2018 Simul.co

#pragma once

#include "CoreMinimal.h"
#include "RemotePlayParameters.h"

#include "libavstream/libavstream.hpp"

#if !UE_BUILD_SHIPPING || !UE_BUILD_TEST
#define WITH_REMOTEPLAY_STATS 1
#endif

class FNetworkPipeline
{
public:
	void Initialize(const FRemotePlayNetworkParameters& InParams, avs::Queue& InInputQueue);
	void Release();
	void Process();

private:
	avs::Pipeline Pipeline;
	avs::Forwarder Forwarder;
	avs::Packetizer Packetizer;
	avs::NetworkSink NetworkSink;

#if WITH_REMOTEPLAY_STATS
	double LastTimestamp = 0.0;
#endif // WITH_REMOTEPLAY_STATS
};
