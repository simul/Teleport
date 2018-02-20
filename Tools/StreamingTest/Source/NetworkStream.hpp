// Copyright (c) 2018 Simul.co

#pragma once

#include <cstdint>
#include <vector>

#include <enet/enet.h>

#include "Interfaces.hpp"

namespace Streaming {

class NetworkStream final : public NetworkIOInterface
{
public:
	NetworkStream();
	~NetworkStream();

	void listen(int port) override;
	void connect(const char* hostName, int port) override;
	void processServer() override;
	bool processClient() override;

	Bitstream read() override;
	void write(const Bitstream& bitstream) override;

private:
	ENetHost* m_host;
	ENetPeer* m_server;
	std::vector<ENetPeer*> m_clients;

	std::vector<char> m_buffer;
	bool m_synced;
};

} // Streaming