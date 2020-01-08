// Copyright 2018 Simul.co
#define WIN32_LEAN_AND_MEAN

#include "NetworkPipeline.h"
#include "RemotePlayModule.h"
#include "RemotePlayMonitor.h"

#if WITH_REMOTEPLAY_STATS
#include "HAL/PlatformTime.h"
#endif // WITH_REMOTEPLAY_STATS

namespace
{
	constexpr double GNetworkPipelineStatInterval = 60.0; // 1s
	constexpr int GNetworkPipelineSocketBufferSize = 16 * 1024 * 1024; // 16MiB
}

void FNetworkPipeline::initialise(ARemotePlayMonitor* m, const FRemotePlayNetworkParameters& InParams,
								  avs::Queue* ColorQueue, avs::Queue* DepthQueue, avs::Queue* GeometryQueue)
{
	assert(ColorQueue);
	Monitor = m;

	avs::NetworkSinkParams SinkParams = {};
	SinkParams.socketBufferSize = GNetworkPipelineSocketBufferSize;
	SinkParams.throttleToRateKpS = std::min(m->ThrottleKpS, (int64)InParams.ClientBandwidthLimit);// Assuming 60Hz on the other size. k per sec
	SinkParams.socketBufferSize = InParams.ClientBufferSize;
	SinkParams.requiredLatencyMs = InParams.RequiredLatencyMs;

	NetworkPipeline::initialise(ColorQueue, DepthQueue, GeometryQueue, SinkParams, InParams.LocalPort, TCHAR_TO_UTF8(*InParams.RemoteIP), InParams.RemotePort);

#if WITH_REMOTEPLAY_STATS
	LastTimestamp = FPlatformTime::Seconds();
#endif // WITH_REMOTEPLAY_STATS
}

void FNetworkPipeline::release()
{
	NetworkPipeline::release();
	Monitor = nullptr;
}

void FNetworkPipeline::process()
{
	NetworkPipeline::process();

#if WITH_REMOTEPLAY_STATS
	std::unique_ptr<avs::NetworkSink>& NetworkSink = getNetworkSink();

	const double Timestamp = FPlatformTime::Seconds();
	if(Timestamp - LastTimestamp >= GNetworkPipelineStatInterval)
	{
		const avs::NetworkSinkCounters Counters = NetworkSink->getCounterValues();
		UE_LOG(LogRemotePlay, Log, TEXT("DP: %llu | NP: %llu | BYTES: %llu"),
			   Counters.decoderPacketsQueued, Counters.networkPacketsSent, Counters.bytesSent);
		LastTimestamp = Timestamp;
	}
	NetworkSink->setDebugStream(Monitor->DebugStream);
	NetworkSink->setDebugNetworkPackets(Monitor->DebugNetworkPackets);
	NetworkSink->setDoChecksums(Monitor->Checksums);
	NetworkSink->setEstimatedDecodingFrequency(Monitor->EstimatedDecodingFrequency);
#endif // WITH_REMOTEPLAY_STATS
}