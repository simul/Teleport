// Copyright (c) 2018 Simul.co

#include "LibStreaming.hpp"

#include "EncoderNV.hpp"
#include "DecoderNV.hpp"
#include "NetworkStream.hpp"

namespace Streaming {

EncoderInterface* createEncoder(Platform platform)
{
	switch(platform) {
	case Platform::NV:
		return new EncoderNV;
	}

	assert(0);
	return nullptr;
}

DecoderInterface* createDecoder(Platform platform)
{
	switch(platform) {
	case Platform::NV:
		return new DecoderNV;
	}

	assert(0);
	return nullptr;
}

NetworkIOInterface* createNetworkIO(NetworkAPI api)
{
	switch(api) {
	case NetworkAPI::ENET:
		return new NetworkStream;
	}

	assert(0);
	return nullptr;
}

} // Streaming