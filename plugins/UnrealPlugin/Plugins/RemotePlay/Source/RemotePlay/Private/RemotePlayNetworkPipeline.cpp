// Copyright 2018 Simul.co

#include "RemotePlayNetworkPipeline.h"
#include "RemotePlayModule.h"
	
FRemotePlayNetworkPipeline::FRemotePlayNetworkPipeline(const FRemotePlayNetworkParameters& InParams, avs::Queue& InInputQueue)
	: Params(InParams)
	, InputQueue(InInputQueue)
	, Packetizer(avs::StreamParser::AVC_AnnexB)
{}

void FRemotePlayNetworkPipeline::Initialize()
{
	Forwarder.configure(1, 1, 64*1024);
	Packetizer.configure(1);
	
	if(!NetworkSink.configure(1, Params.LocalPort, TCHAR_TO_UTF8(*Params.RemoteIP), Params.RemotePort))
	{
		UE_LOG(LogRemotePlay, Error, TEXT("Failed to configure network sink"));
		return;
	}

	if(!Pipeline.add({&InputQueue, &Forwarder, &Packetizer, &NetworkSink}))
	{
		UE_LOG(LogRemotePlay, Error, TEXT("Error configuring the network pipeline"));
		return;
	}
}
	
void FRemotePlayNetworkPipeline::Release()
{
	NetworkSink.deconfigure();
	Packetizer.deconfigure();
	Forwarder.deconfigure();
}
	
void FRemotePlayNetworkPipeline::Process()
{
	const avs::Result result = Pipeline.process();
	if(!result && result != avs::Result::IO_Empty)
	{
		UE_LOG(LogRemotePlay, Warning, TEXT("Network pipeline processing encountered an error"));
	}
}
