// Copyright (c) 2018 Simul.co

#pragma once

#ifndef LIBSTREAMING
#define LIBSTREAMING
#endif

#include "Interfaces.hpp"

namespace Streaming {

enum class Platform
{
	NV,
};
enum class NetworkAPI
{
	ENET,
};

EncoderInterface* createEncoder(Platform platform);
DecoderInterface* createDecoder(Platform platform);
NetworkIOInterface* createNetworkIO(NetworkAPI api);

} // Streaming