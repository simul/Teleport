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
	FNetworkPipeline();
	void Initialize(class ARemotePlayMonitor *m,const FRemotePlayNetworkParameters& InParams, avs::Queue* ColorQueue, avs::Queue* DepthQueue, avs::Queue* GeometryQueue);
	void Release();
	void Process();
	avs::Pipeline *GetAvsPipeline();
	float GetBandWidthKPS() const;
private: 
	struct VideoPipe
	{
		avs::Queue* SourceQueue;
		avs::Forwarder Forwarder;
		avs::Packetizer Packetizer;
	};
	struct GeometryPipe
	{
		avs::Queue* SourceQueue;
		avs::Forwarder Forwarder;
		avs::Packetizer Packetizer;
	};
	TUniquePtr<avs::Pipeline> Pipeline; 
	TArray<VideoPipe> VideoPipes;
	TArray<GeometryPipe> GeometryPipes;
	TUniquePtr<avs::NetworkSink> NetworkSink;
	class ARemotePlayMonitor *Monitor;
#if WITH_REMOTEPLAY_STATS
	double LastTimestamp = 0.0;
#endif // WITH_REMOTEPLAY_STATS
};
