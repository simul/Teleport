// Copyright (c) 2018 Simul.co

#pragma once

#include <cstdint>
#include <vector>

#include <enet/enet.h>

#include "Interfaces.hpp"

class NetworkStream : public IOInterface
{
public:
	NetworkStream();
	~NetworkStream();

	void listen(int port);
	void connect(const char* hostName, int port);

	void processServer();
	bool processClient();

	Bitstream read() override;
	void write(const Bitstream& bitstream) override;

private:
	ENetHost* m_host;
	ENetPeer* m_server;
	std::vector<ENetPeer*> m_clients;

	std::vector<char> m_buffer;
	bool m_synced;
};
