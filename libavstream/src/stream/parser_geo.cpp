// (c) Copyright 2018-2022 Simul Software Ltd

#include "stream/parser_geo.hpp"
#include <string.h>
#include <iostream>


namespace avs
{
	GeometryStreamParser::GeometryStreamParser()
		: m_node(nullptr)
		, m_callback(nullptr)
		, m_inputNodeIndex(0)
	{
	}

	Result GeometryStreamParser::configure(PipelineNode* node, OnPacketFn callback, uint32_t inputNodeIndex)
	{
		m_node = node;
		m_callback = callback;
		m_inputNodeIndex = inputNodeIndex;
		return Result::OK;
	}

	Result GeometryStreamParser::parse(const char* buffer, size_t bufferSize)
	{
		Result result = Result::OK;

		static const size_t HEADER_SIZE = sizeof(size_t);

		size_t parseOffset = 0;

		while (parseOffset < bufferSize)
		{
			size_t dataSize;
			memcpy(&dataSize, buffer + parseOffset, HEADER_SIZE);
			GeometryPayloadType type;
			if(parseOffset+sizeof(size_t)<bufferSize)
				memcpy(&type,buffer+parseOffset+sizeof(size_t),sizeof(GeometryPayloadType));
			if(dataSize+parseOffset>bufferSize)
			{
				std::cerr<<"Bad dataSize for "<<stringOf(type)<<".\n";
				break;
			}
			//std::cout<<" dataSize "<<dataSize<<" for "<<stringOf(type)<<"\n";
			result = m_callback(m_node, m_inputNodeIndex, buffer, dataSize, parseOffset + HEADER_SIZE, true);
			if (!result)
			{
				break;
			}
			parseOffset += (HEADER_SIZE + dataSize);
		}

		return result;
	}

	Result GeometryStreamParser::flush()
	{
		return Result::OK;
	}

} // avs