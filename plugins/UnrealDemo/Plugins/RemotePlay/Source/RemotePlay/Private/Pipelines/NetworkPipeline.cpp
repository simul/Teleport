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
	constexpr int    GNetworkPipelineSocketBufferSize = 16 * 1024 * 1024; // 16MiB
}

FNetworkPipeline::FNetworkPipeline() :Monitor(nullptr)
{
}

void FNetworkPipeline::Initialize(ARemotePlayMonitor *m,const FRemotePlayNetworkParameters& InParams
	, avs::Queue* ColorQueue, avs::Queue* DepthQueue, avs::Queue* GeometryQueue)
{
	check(ColorQueue);
	Monitor = m;
	Pipeline.Reset(new avs::Pipeline);
	NetworkSink.Reset(new avs::NetworkSink);
	VideoPipes.SetNum(DepthQueue?2:1);
	VideoPipes[0].SourceQueue = ColorQueue;
	if (DepthQueue)
		VideoPipes[1].SourceQueue = DepthQueue;
	GeometryPipes.SetNum(1);
	GeometryPipes[0].SourceQueue = GeometryQueue;

	int NumInputs = VideoPipes.Num() + GeometryPipes.Num();
	avs::NetworkSinkParams SinkParams = {};
	SinkParams.socketBufferSize = GNetworkPipelineSocketBufferSize;
	SinkParams.throttleToRateKpS = std::min(m->ThrottleKpS,(int64)InParams.ClientBandwidthLimit);// Assuming 60Hz on the other size. k per sec
	SinkParams.socketBufferSize = InParams.ClientBufferSize;
	if (!NetworkSink->configure(NumInputs, InParams.LocalPort, TCHAR_TO_UTF8(*InParams.RemoteIP), InParams.RemotePort, SinkParams))
	{
		UE_LOG(LogRemotePlay, Error, TEXT("Failed to configure network sink"));
		return;
	}


	// Each video stream has an input Queue, a forwarder, and a packetizer.
	// The Geometry queue consists of an input Queue, a Forwarder, and a Geometry packetizer.
	for (int32_t i = 0; i < VideoPipes.Num(); ++i)
	{
		auto &Pipe=VideoPipes[i];
		Pipe.Forwarder.configure(1, 1, 64 * 1024);
		Pipe.Packetizer.configure(avs::StreamParserInterface::Create(avs::StreamParserType::AVC_AnnexB), 1 , 50+i);
		if (!Pipeline->link({ Pipe.SourceQueue, &Pipe.Forwarder, &Pipe.Packetizer }) || !avs::Node::link(Pipe.Packetizer, *NetworkSink))
		{
			UE_LOG(LogRemotePlay, Error, TEXT("Failed to configure network video pipeline"));
			return;
		}
	}
	for (int32_t i = 0; i < GeometryPipes.Num(); ++i)
	{
		auto &Pipe = GeometryPipes[i];
		Pipe.Forwarder.configure(1, 1, 64 * 1024);
		Pipe.Packetizer.configure(avs::StreamParserInterface::Create(avs::StreamParserType::Geometry), 1 , 100 + i);
		if (!Pipeline->link({ Pipe.SourceQueue, &Pipe.Forwarder, &Pipe.Packetizer }) || !avs::Node::link(Pipe.Packetizer, *NetworkSink))
		{
			UE_LOG(LogRemotePlay, Error, TEXT("Failed to configure network video pipeline"));
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
	VideoPipes.Empty();
	GeometryPipes.Empty();
	Monitor = nullptr;
}

void FNetworkPipeline::Process()
{
	check(Pipeline.IsValid());
	check(NetworkSink.IsValid());

	const avs::Result result = Pipeline->process();
	if (!result && result != avs::Result::IO_Empty)
	{
		UE_LOG(LogRemotePlay, Warning, TEXT("Network pipeline processing encountered an error"));
	}

#if WITH_REMOTEPLAY_STATS
	const double Timestamp = FPlatformTime::Seconds();
	if (Timestamp - LastTimestamp >= GNetworkPipelineStatInterval)
	{
		const avs::NetworkSinkCounters Counters = NetworkSink->getCounterValues();
		UE_LOG(LogRemotePlay, Log, TEXT("DP: %llu | NP: %llu | BYTES: %llu"),
			Counters.decoderPacketsQueued, Counters.networkPacketsSent, Counters.bytesSent);
		LastTimestamp = Timestamp;
	}
	NetworkSink->setDebugStream(Monitor->DebugStream);
	NetworkSink->setDoChecksums(Monitor->Checksums);
	NetworkSink->setEstimatedDecodingFrequency(Monitor->EstimatedDecodingFrequency);
#endif // WITH_REMOTEPLAY_STATS
}

avs::Pipeline *FNetworkPipeline::GetAvsPipeline()
{
	return Pipeline.Get();
}
float FNetworkPipeline::GetBandWidthKPS() const
{
	if (!NetworkSink.IsValid())
		return 0.0f;
	return NetworkSink->getBandwidthKPerS();
}