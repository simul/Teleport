#pragma once

struct GLFWwindow;
struct RendererDevice;

enum class EncoderInputFormat
{
	ARGB,
	NV12,
};

class RendererInterface
{
public:
	virtual GLFWwindow* initialize(int width, int height) = 0;
	virtual void render() = 0;

	virtual RendererDevice* getDevice() const = 0;
};

class EncoderInterface
{
public:
	virtual ~EncoderInterface() = default;

	virtual void initialize(RendererDevice* device) = 0;
	virtual void shutdown() = 0;
};