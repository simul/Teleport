// Copyright (c) 2018 Simul.co

#pragma once

namespace Streaming {

class Bitstream
{
public:
	Bitstream()
		: pData(nullptr)
		, numBytes(0)
		, m_mappedMemory(false)
	{}

	explicit Bitstream(size_t numBytes)
		: pData(new char[numBytes])
		, numBytes(numBytes)
		, m_mappedMemory(false)
	{}

	Bitstream(char* data, size_t numBytes)
		: pData(data)
		, numBytes(numBytes)
		, m_mappedMemory(true)
	{}

	Bitstream(const Bitstream&) = delete;

	Bitstream(Bitstream&& other)
		: pData(other.pData)
		, numBytes(other.numBytes)
		, m_mappedMemory(other.m_mappedMemory)
	{
		other.pData = nullptr;
		other.numBytes = 0;
	}

	~Bitstream()
	{
		if(!m_mappedMemory) {
			delete[] pData;
		}
	}

	operator bool() const
	{
		return pData != nullptr;
	}
	
	char* pData;
	size_t numBytes;

private:
	const bool m_mappedMemory;
};

} // Streaming
