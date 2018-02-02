#pragma once

#include "Interfaces.hpp"

#include <Windows.h>
#include <nvEncodeAPI.h>

class EncoderNV : public EncoderInterface
{
public:
	EncoderNV();
	~EncoderNV();

	void initialize(RendererDevice* device) override;
	void shutdown() override;

private:
	struct EncodeConfig
	{
		GUID encodeGUID;
		GUID presetGUID;
		NV_ENC_BUFFER_FORMAT format;
	};
	EncodeConfig chooseEncodeConfig(GUID requestedPresetGUID) const;

	HMODULE m_hLibrary;
	void* m_encoder;
	
	NV_ENCODE_API_FUNCTION_LIST api;
};
