// Copyright 2018 Simul.co

#pragma once

#include "CoreMinimal.h"

#include "RemotePlayParameters.h"

#include "libavstream/libavstream.hpp"

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
};
