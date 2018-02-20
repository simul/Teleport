// Copyright (c) 2018 Simul.co

#pragma once

#include <fstream>
#include "Interfaces.hpp"

namespace Streaming {

class FileReader final : public IOInterface
{
public:
	FileReader(const char* filename, size_t packetSize);
	Bitstream read() override;

	operator bool() const { return !m_stream.eof(); }

private:
	const size_t m_packetSize;
	std::ifstream m_stream;
};

class FileWriter final : public IOInterface
{
public:
	FileWriter(const char* filename);
	void write(const Bitstream& bitstream) override;

private:
	std::ofstream m_stream;
};

} // Streaming