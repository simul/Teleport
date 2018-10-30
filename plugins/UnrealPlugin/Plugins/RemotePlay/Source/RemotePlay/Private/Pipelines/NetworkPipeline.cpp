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
	
void FNetworkPipeline::Initialize(const FRemotePlayNetworkParameters& InParams, avs::Queue* ColorQueue, avs::Queue* DepthQueue)
{
	check(ColorQueue);

	const uint32_t NumInputs = (DepthQueue != nullptr) ? 2 : 1;
	avs::Queue* const Inputs[] = { ColorQueue, DepthQueue };

	Pipeline.Reset(new avs::Pipeline);
	NetworkSink.Reset(new avs::NetworkSink);

	avs::NetworkSinkParams SinkParams = {};
	SinkParams.socketBufferSize = GNetworkPipelineSocketBufferSize;
	if(!NetworkSink->configure(NumInputs, InParams.LocalPort, TCHAR_TO_UTF8(*InParams.RemoteIP), InParams.RemotePort, SinkParams))
	{
		UE_LOG(LogRemotePlay, Error, TEXT("Failed to configure network sink"));
		return;
	}

	Forwarder.SetNum(NumInputs);
	Packetizer.SetNum(NumInputs);
	for(uint32_t i=0; i<NumInputs; ++i)
	{
		Forwarder[i].configure(1, 1, 64 * 1024);
		Packetizer[i].configure(1);
		if(!Pipeline->link({ Inputs[i], &Forwarder[i], &Packetizer[i] }) || !avs::Node::link(Packetizer[i], *NetworkSink))
		{
			UE_LOG(LogRemotePlay, Error, TEXT("Failed to configure network pipeline"));
			return;
		}
	}
	Pipeline->add(NetworkSink.Get());

#if WITH_REMOTEPLAY_STATS
	LastTimestamp = FPlatformTime::Seconds();
#endif // WITH_REMOTEPLAY_STATS
}
	
void FNetworkPipeline::Release()
{
	Pipeline.Reset();
	NetworkSink.Reset();
	Forwarder.Empty();
	Packetizer.Empty();
}
	
void FNetworkPipeline::Process()
{
	check(Pipeline.IsValid());
	check(NetworkSink.IsValid());

	const avs::Result result = Pipeline->process();
	if(!result && result != avs::Result::IO_Empty)
	{
		UE_LOG(LogRemotePlay, Warning, TEXT("Network pipeline processing encountered an error"));
	}

#if WITH_REMOTEPLAY_STATS
	const double Timestamp = FPlatformTime::Seconds();
	if(Timestamp - LastTimestamp >= GNetworkPipelineStatInterval)
	{
		const avs::NetworkSinkCounters Counters = NetworkSink->getCounterValues();
		UE_LOG(LogRemotePlay, Log, TEXT("DP: %llu | NP: %llu | BYTES: %llu"),
			Counters.decoderPacketsQueued, Counters.networkPacketsSent, Counters.bytesSent);
		LastTimestamp = Timestamp;
	}
#endif // WITH_REMOTEPLAY_STATS
}
