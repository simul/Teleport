// libavstream
// (c) Copyright 2018-2019 Simul Software Ltd

#pragma once

#include <memory>
#include <vector>
#include <queue>

#include <common_p.hpp>
#include <node_p.hpp>
#include <libavstream/packetizer.hpp>
#include <libavstream/stream/parser_interface.hpp>

namespace avs
{
	struct Packetizer::Private final : public Node::Private
	{
		AVSTREAM_PRIVATEINTERFACE(Packetizer, Node)

		static Result onPacketParsed(Node* node, uint32_t inputNodeIndex, const char* buffer, size_t dataSize, size_t dataOffset, bool isLastPayload);

		std::unique_ptr<StreamParserInterface> m_parser;
		std::vector<char> m_buffer;
		int streamIndex=0;

		Result process(uint32_t timestamp);
	};
} // avs