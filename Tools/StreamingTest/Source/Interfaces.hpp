// Copyright (c) 2018 Simul.co

#pragma once

#include <cassert>
#include <cstdint>

#include "Bitstream.hpp"

#ifndef LIBSTREAMING
struct GLFWwindow;
#endif

namespace Streaming {

struct RendererDevice;

struct Surface
{
	int width;
	int height;
	void* pResource;
};
struct Buffer
{
	int size;
	void* pResource;
};

enum class SurfaceFormat
{
	Unknown = 0,
	ARGB,
	ABGR,
	NV12,
};

class RendererInterface
{
public:
	virtual ~RendererInterface() = default;

#ifndef LIBSTREAMING
	virtual GLFWwindow* initialize(const char* title, int width, int height) = 0;
	virtual void renderScene() = 0;
	virtual void renderVideo() = 0;
	virtual void renderSurface() = 0;
#endif

	virtual Surface createSurface(SurfaceFormat format) = 0;
	virtual void releaseSurface(Surface& surface) = 0;

	virtual Buffer createVideoBuffer(SurfaceFormat format, int pitch) = 0;
	virtual void releaseVideoBuffer(Buffer& buffer) = 0;

	virtual RendererDevice* getDevice() const = 0;
};

class EncoderInterface
{
public:
	virtual ~EncoderInterface() = default;

	virtual void initialize(RendererInterface* renderer, int width, int height, uint64_t idrFrequency) = 0;
	virtual void shutdown() = 0;
	virtual void encode(uint64_t timestamp) = 0;

	virtual Bitstream lock() = 0;
	virtual void unlock() = 0;
	
	virtual SurfaceFormat getInputFormat() const = 0;
};

class DecoderInterface
{
public:
	virtual ~DecoderInterface() = default;

	virtual void initialize(RendererInterface* renderer, int width, int height) = 0;
	virtual void shutdown() = 0;
	virtual void decode(Bitstream& stream) = 0;
};

class IOInterface
{
public:
	virtual ~IOInterface() = default;

	virtual Bitstream read()
	{
		assert(false); // Read operation not supported.
		return Bitstream{};
	}
	virtual void write(const Bitstream&)
	{
		assert(false); // Write operation not supported.
	}
};

class NetworkIOInterface : public IOInterface
{
public:
	virtual ~NetworkIOInterface() = default;

	virtual void listen(int port) = 0;
	virtual void connect(const char* hostName, int port) = 0;
	virtual void processServer() = 0;
	virtual bool processClient() = 0;
};

} // Streaming