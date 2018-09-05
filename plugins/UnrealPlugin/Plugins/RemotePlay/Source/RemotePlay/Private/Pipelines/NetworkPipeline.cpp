// Copyright 2018 Simul.co

#include "NetworkPipeline.h"
#include "RemotePlayModule.h"
	
void FNetworkPipeline::Initialize(const FRemotePlayNetworkParameters& InParams, avs::Queue& InInputQueue)
{
	Forwarder.configure(1, 1, 64*1024);
	Packetizer.configure(1);
	
	if(!NetworkSink.configure(1, InParams.LocalPort, TCHAR_TO_UTF8(*InParams.RemoteIP), InParams.RemotePort))
	{
		UE_LOG(LogRemotePlay, Error, TEXT("Failed to configure network sink"));
		return;
	}

	if(!Pipeline.link({&InInputQueue, &Forwarder, &Packetizer, &NetworkSink}))
	{
		UE_LOG(LogRemotePlay, Error, TEXT("Error configuring the network pipeline"));
		return;
	}
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
}
