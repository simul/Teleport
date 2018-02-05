// Copyright (c) 2018 Simul.co

#include <stdexcept>

#include "FileIO.hpp"

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
	m_stream.write(reinterpret_cast<char*>(bitstream.pPosition), bitstream.numBytes);
}
