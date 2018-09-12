// Copyright 2018 Simul.co

#include "NetworkPipeline.h"
#include "RemotePlayModule.h"

#if WITH_REMOTEPLAY_STATS
#include "HAL/PlatformTime.h"
#endif // WITH_REMOTEPLAY_STATS

namespace {

constexpr double GNetworkPipelineStatInterval = 1.0; // 1s
constexpr int    GNetworkPipelineSocketBufferSize = 16 * 1024 * 1024; // 16MiB

}
	
void FNetworkPipeline::Initialize(const FRemotePlayNetworkParameters& InParams, avs::Queue& InInputQueue)
{
	Forwarder.configure(1, 1, 64 * 1024);
	Packetizer.configure(1);
	
	avs::NetworkSinkParams SinkParams = {};
	SinkParams.socketBufferSize = GNetworkPipelineSocketBufferSize;
	if(!NetworkSink.configure(1, InParams.LocalPort, TCHAR_TO_UTF8(*InParams.RemoteIP), InParams.RemotePort, SinkParams))
	{
		UE_LOG(LogRemotePlay, Error, TEXT("Failed to configure network sink"));
		return;
	}

	if(!Pipeline.link({&InInputQueue, &Forwarder, &Packetizer, &NetworkSink}))
	{
		UE_LOG(LogRemotePlay, Error, TEXT("Error configuring the network pipeline"));
		return;
	}

#if WITH_REMOTEPLAY_STATS
	LastTimestamp = FPlatformTime::Seconds();
#endif // WITH_REMOTEPLAY_STATS
}
	
void FNetworkPipeline::Release()
{
	NetworkSink.deconfigure();
	Packetizer.deconfigure();
	Forwarder.deconfigure();
}
	
void FNetworkPipeline::Process()
{
	const avs::Result result = Pipeline.process();
	if(!result && result != avs::Result::IO_Empty)
	{
		UE_LOG(LogRemotePlay, Warning, TEXT("Network pipeline processing encountered an error"));
	}

#if WITH_REMOTEPLAY_STATS
	const double Timestamp = FPlatformTime::Seconds();
	if(Timestamp - LastTimestamp >= GNetworkPipelineStatInterval)
	{
		const avs::NetworkSinkCounters Counters = NetworkSink.getCounterValues();
		UE_LOG(LogRemotePlay, Log, TEXT("DP: %llu | NP: %llu | BYTES: %llu"),
			Counters.decoderPacketsQueued, Counters.networkPacketsSent, Counters.bytesSent);
		LastTimestamp = Timestamp;
	}
#endif // WITH_REMOTEPLAY_STATS
}
