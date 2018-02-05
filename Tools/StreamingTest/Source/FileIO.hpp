// Copyright (c) 2018 Simul.co

#pragma once

#include <fstream>
#include "Interfaces.hpp"

class FileWriter final : public IOInterface
{
public:
	FileWriter(const char* filename);
	void write(const Bitstream& bitstream) override;

private:
	std::ofstream m_stream;
};