// Copyright 2018 Simul.co

#pragma once

#include "CoreMinimal.h"

#include "RemotePlayParameters.h"

#include "libavstream/libavstream.hpp"

class FRemotePlayNetworkPipeline
{
public:
	FRemotePlayNetworkPipeline(const FRemotePlayNetworkParameters& InParams, avs::Queue& InInputQueue);
	
	void Initialize();
	void Release();
	void Process();

private:
	const FRemotePlayNetworkParameters Params;

	avs::Pipeline Pipeline;
	avs::Queue& InputQueue;
	avs::Forwarder Forwarder;
	avs::Packetizer Packetizer;
	avs::NetworkSink NetworkSink;
};
