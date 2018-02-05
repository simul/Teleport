// Copyright (c) 2018 Simul.co

#pragma once

#include <cassert>

struct GLFWwindow;
struct RendererDevice;

struct Surface
{
	int width;
	int height;
	void* pResource;
};
struct Bitstream
{
	void* pPosition;
	size_t numBytes;
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

	virtual GLFWwindow* initialize(int width, int height) = 0;
	virtual void render() = 0;

	virtual Surface createSurface(SurfaceFormat format) = 0;
	virtual void releaseSurface(Surface& surface) = 0;

	virtual RendererDevice* getDevice() const = 0;
};

class EncoderInterface
{
public:
	virtual ~EncoderInterface() = default;

	virtual void initialize(RendererDevice* device, int width, int height) = 0;
	virtual void shutdown() = 0;
	virtual void encode(uint64_t timestamp) = 0;

	virtual Bitstream lock() = 0;
	virtual void unlock() = 0;

	virtual void registerSurface(const Surface& surface) = 0;
	virtual SurfaceFormat getInputFormat() const = 0;
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
	virtual void write(const Bitstream& bitstream)
	{
		assert(false); // Write operation not supported.
	}
};
