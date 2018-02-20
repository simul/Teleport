// Copyright (c) 2018 Simul.co

#include <stdexcept>
#include <algorithm>
#include <iterator>

#include "NetworkStream.hpp"

#define SCAN_FOR_STARTCODES 0

namespace {
	const char g_startCode[] = { 0x00, 0x00, 0x00, 0x01 };
	const enet_uint32 g_timeout = 5000;
}

using namespace Streaming;

NetworkStream::NetworkStream()
	: m_host(nullptr)
	, m_server(nullptr)
#if SCAN_FOR_STARTCODES
	, m_synced(false)
#else
	, m_synced(true)
#endif
{
	static bool enetInitialized = false;
	if(!enetInitialized) {
		if(enet_initialize() != 0) {
			throw std::runtime_error("Failed to initialize ENET library");
		}
		enetInitialized = true;
	}
}

NetworkStream::~NetworkStream()
{
	int expectedDisconnectMsgs = 0;

	if(m_server) {
		enet_peer_disconnect(m_server, 0);
		++expectedDisconnectMsgs;
	}
	else {
		for(ENetPeer* client : m_clients) {
			enet_peer_disconnect(client, 0);
			++expectedDisconnectMsgs;
		}
	}

	ENetEvent event;
	while(expectedDisconnectMsgs > 0 && enet_host_service(m_host, &event, g_timeout)) {
		switch(event.type) {
		case ENET_EVENT_TYPE_RECEIVE:
			enet_packet_destroy(event.packet);
			break;
		case ENET_EVENT_TYPE_DISCONNECT:
			--expectedDisconnectMsgs;
			break;
		}
	}

	if(m_host) {
		enet_host_flush(m_host);
		enet_host_destroy(m_host);
	}
}

void NetworkStream::listen(int port)
{
	ENetAddress address = { ENET_HOST_ANY, (enet_uint16)port };
	m_host = enet_host_create(&address, 8, 1, 0, 0);
	if(!m_host) {
		throw std::runtime_error("Failed to create ENET listen server");
	}
}
	
void NetworkStream::connect(const char* hostName, int port)
{
	m_host = enet_host_create(nullptr, 1, 1, 0, 0);
	if(!m_host) {
		throw std::runtime_error("Failed to create ENET client host");
	}

	ENetAddress serverAddress;
	enet_address_set_host(&serverAddress, hostName);
	serverAddress.port = (enet_uint16)port;

	m_server = enet_host_connect(m_host, &serverAddress, 1, 0);
	if(!m_server) {
		throw std::runtime_error("Failed to initiate ENET connection");
	}

	ENetEvent event;
	if(enet_host_service(m_host, &event, g_timeout) > 0 && event.type == ENET_EVENT_TYPE_CONNECT) {
		std::printf("Connected to streaming server: %s:%d\n", hostName, port);
	}
	else {
		enet_peer_reset(m_server);
		throw std::runtime_error("Failed to connect to ENET listen server");
	}
}

Bitstream NetworkStream::read()
{
	if(m_synced && m_buffer.size() > 0) {
		Bitstream bitstream{m_buffer.size()};
		std::memcpy(bitstream.pData, &m_buffer[0], m_buffer.size());
		m_buffer.clear();
		return bitstream;
	}
	return Bitstream{};
}

void NetworkStream::processServer()
{
	ENetEvent event;
	char peer_ip[64];

	while(enet_host_service(m_host, &event, 0)) {
		switch(event.type) {
		case ENET_EVENT_TYPE_CONNECT:
			m_clients.push_back(event.peer);
			enet_address_get_host_ip(&event.peer->address, peer_ip, sizeof(peer_ip)-1);
			std::printf("Client connected: %s:%u\n", peer_ip, event.peer->address.port);
			break;
		case ENET_EVENT_TYPE_DISCONNECT:
			{
				auto it = std::find(m_clients.begin(), m_clients.end(), event.peer);
				if(it != m_clients.end()) {
					m_clients.erase(it);
				}
			}
			enet_address_get_host_ip(&event.peer->address, peer_ip, sizeof(peer_ip)-1);
			std::printf("Client disconnected: %s:%u\n", peer_ip, event.peer->address.port);
			break;
		case ENET_EVENT_TYPE_RECEIVE:
			enet_packet_destroy(event.packet);
			break;
		}
	}
}

bool NetworkStream::processClient()
{
	ENetEvent event;

	while(enet_host_service(m_host, &event, 0)) {
		switch(event.type) {
		case ENET_EVENT_TYPE_RECEIVE:
			m_buffer.insert(m_buffer.end(), event.packet->data, event.packet->data + event.packet->dataLength);
			enet_packet_destroy(event.packet);
#if SCAN_FOR_STARTCODES
			if(!m_synced && m_buffer.size() > 4) {
				auto it = std::search(m_buffer.begin(), m_buffer.end(), g_startCode, g_startCode + 4);
				if(it != m_buffer.end()) {
					const size_t N = std::distance(it, m_buffer.end());
					std::copy(it, m_buffer.end(), m_buffer.begin());
					m_buffer.resize(N);
					m_synced = true;
				}
				else
				{
					const size_t N = m_buffer.size();
					std::copy(m_buffer.end()-3, m_buffer.end(), m_buffer.begin());
					m_buffer.resize(3);
				}
			}
#endif
			break;
		case ENET_EVENT_TYPE_DISCONNECT:
			std::printf("Disconnected from server\n");
			m_server = nullptr;
			return false;
		}
	}
	return true;
}
	
void NetworkStream::write(const Bitstream& bitstream)
{
	ENetPacket* packet = enet_packet_create(bitstream.pData, bitstream.numBytes, ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT);
	enet_host_broadcast(m_host, 0, packet);
	enet_host_flush(m_host);
}
