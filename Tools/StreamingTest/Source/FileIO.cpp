// Copyright (c) 2018 Simul.co

#include <stdexcept>

#include "FileIO.hpp"

using namespace Streaming;

FileReader::FileReader(const char* filename, size_t packetSize)
	: m_packetSize(packetSize)
{
	m_stream.open(filename, std::ios::binary);
	if(!m_stream) {
		throw std::runtime_error("Failed to open input file");
	}
}
	
Bitstream FileReader::read()
{
	assert(m_stream.is_open());

	Bitstream bitstream{m_packetSize};
	m_stream.read(bitstream.pData, m_packetSize);
	bitstream.numBytes = m_stream.gcount();
	return bitstream;
}

FileWriter::FileWriter(const char* filename)
{
	m_stream.open(filename, std::ios::binary | std::ios::trunc);
	if(!m_stream) {
		throw std::runtime_error("Failed to open ouput file");
	}
}

void FileWriter::write(const Bitstream& bitstream)
{
	assert(m_stream.is_open());
	m_stream.write(reinterpret_cast<char*>(bitstream.pData), bitstream.numBytes);
}
