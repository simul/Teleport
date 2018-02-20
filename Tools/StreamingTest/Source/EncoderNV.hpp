// Copyright (c) 2018 Simul.co

#pragma once

#include "Interfaces.hpp"

#include <Windows.h>
#include <nvEncodeAPI.h>

namespace Streaming {

class EncoderNV final : public EncoderInterface
{
public:
	EncoderNV();
	~EncoderNV();

	void initialize(RendererInterface* renderer, int width, int height, uint64_t idrFrequency) override;
	void shutdown() override;
	void encode(uint64_t timestamp) override;
	
	Bitstream lock() override;
	void unlock() override;
	
	SurfaceFormat getInputFormat() const override;

private:
	void registerSurface(const Surface& surface);

	RendererInterface* m_renderer;

	struct EncodeConfig
	{
		GUID encodeGUID;
		GUID presetGUID;
		NV_ENC_BUFFER_FORMAT format;
		NV_ENC_CONFIG config;
	};
	EncodeConfig chooseEncodeConfig(GUID requestedPresetGUID) const;

	HMODULE m_hLibrary;
	void* m_encoder;

	NV_ENC_BUFFER_FORMAT m_bufferFormat;
	NV_ENC_REGISTERED_PTR m_inputBufferPtr;
	NV_ENC_OUTPUT_PTR m_outputBufferPtr;
	uint64_t m_idrFrequency;

	Surface m_registeredSurface;
	
	NV_ENCODE_API_FUNCTION_LIST api;
};

} // Streaming