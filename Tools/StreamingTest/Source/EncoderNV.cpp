// Copyright (c) 2018 Simul.co

#include <stdexcept>
#include <vector>
#include <map>
#include <functional>

#include "EncoderNV.hpp"

#define NVFAILED(x) ((x) != NV_ENC_SUCCESS)

EncoderNV::EncoderNV()
	: api({NV_ENCODE_API_FUNCTION_LIST_VER})
	, m_bufferFormat(NV_ENC_BUFFER_FORMAT::NV_ENC_BUFFER_FORMAT_UNDEFINED)
	, m_inputBufferPtr(nullptr)
	, m_outputBufferPtr(nullptr)
	, m_registeredSurface({0, 0, nullptr})
{
	m_hLibrary = LoadLibrary(
#if _WIN64
		L"nvEncodeAPI64.dll"
#else
		L"nvEncodeAPI.dll"
#endif
	);
	if(!m_hLibrary) {
		throw std::runtime_error("Failed to load nvEncodeAPI library");
	}

	typedef NVENCSTATUS (NVENCAPI *NvEncodeAPICreateInstanceFUNC)(NV_ENCODE_API_FUNCTION_LIST*);
	NvEncodeAPICreateInstanceFUNC NvEncodeAPICreateInstance = (NvEncodeAPICreateInstanceFUNC)GetProcAddress(m_hLibrary, "NvEncodeAPICreateInstance");
	if(!NvEncodeAPICreateInstance) {
		throw std::runtime_error("Unable to find NvEncodeAPI interface entry point");
	}
	if(NvEncodeAPICreateInstance(&api) != NV_ENC_SUCCESS) {
		throw std::runtime_error("Failed to initialize NvEncodeAPI");
	}
}

EncoderNV::~EncoderNV()
{
	if(m_hLibrary) {
		FreeLibrary(m_hLibrary);
	}
}

void EncoderNV::initialize(std::shared_ptr<RendererInterface> renderer, int width, int height)
{
	m_renderer = renderer;

	{
		NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS params = {};
		params.apiVersion = NVENCAPI_VERSION;
		params.version    = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
		params.device     = reinterpret_cast<void*>(renderer->getDevice());
		// Assume DirectX device for now.
		params.deviceType = NV_ENC_DEVICE_TYPE::NV_ENC_DEVICE_TYPE_DIRECTX;
		
		if(NVFAILED(api.nvEncOpenEncodeSessionEx(&params, &m_encoder))) {
			throw std::runtime_error("Failed to open NVENC encode session");
		}
	}

	const EncodeConfig config = chooseEncodeConfig(NV_ENC_PRESET_LOW_LATENCY_HQ_GUID);
	m_bufferFormat = config.format;
	{
		NV_ENC_INITIALIZE_PARAMS params = {};
		params.version = NV_ENC_INITIALIZE_PARAMS_VER;
		params.encodeGUID = config.encodeGUID;
		params.presetGUID = config.presetGUID;
		params.encodeWidth = width;
		params.encodeHeight = height;
		params.enablePTD = true;
		params.enableEncodeAsync = false;

		if(NVFAILED(api.nvEncInitializeEncoder(m_encoder, &params))) {
			throw std::runtime_error("Failed to initialize NVENC encoder");
		}
	}

	{
		NV_ENC_CREATE_BITSTREAM_BUFFER params = {};
		params.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
		
		if(NVFAILED(api.nvEncCreateBitstreamBuffer(m_encoder, &params))) {
			throw std::runtime_error("Failed to create output bitstream buffer");
		}
		m_outputBufferPtr = params.bitstreamBuffer;
	}

	registerSurface(renderer->createSurface(getInputFormat()));
}

void EncoderNV::shutdown()
{
	NV_ENC_PIC_PARAMS flushParams = {};
	flushParams.version = NV_ENC_PIC_PARAMS_VER;
	flushParams.encodePicFlags = NV_ENC_PIC_FLAG_EOS;
	if(NVFAILED(api.nvEncEncodePicture(m_encoder, &flushParams))) {
		throw std::runtime_error("Failed to flush the encoder");
	}

	if(m_inputBufferPtr) {
		api.nvEncUnregisterResource(m_encoder, m_inputBufferPtr);
		m_inputBufferPtr = nullptr;
	}
	if(m_outputBufferPtr) {
		api.nvEncDestroyBitstreamBuffer(m_encoder, m_outputBufferPtr);
		m_outputBufferPtr = nullptr;
	}
	api.nvEncDestroyEncoder(m_encoder);

	m_renderer->releaseSurface(m_registeredSurface);
}

void EncoderNV::encode(uint64_t timestamp)
{
	NV_ENC_MAP_INPUT_RESOURCE resource = {};
	resource.version = NV_ENC_MAP_INPUT_RESOURCE_VER;
	resource.registeredResource = m_inputBufferPtr;
	if(NVFAILED(api.nvEncMapInputResource(m_encoder, &resource))) {
		throw std::runtime_error("Failed to map input resource");
	}

	NV_ENC_PIC_PARAMS encParams = {};
	encParams.version = NV_ENC_PIC_PARAMS_VER;
	encParams.bufferFmt = resource.mappedBufferFmt;
	encParams.inputBuffer = resource.mappedResource;
	encParams.outputBitstream = m_outputBufferPtr;
	encParams.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
	encParams.inputWidth = m_registeredSurface.width;
	encParams.inputHeight = m_registeredSurface.height;
	encParams.inputPitch = encParams.inputWidth;
	encParams.inputTimeStamp = timestamp;
	if(NVFAILED(api.nvEncEncodePicture(m_encoder, &encParams))) {
		throw std::runtime_error("Failed to encode picture frame");
	}

	api.nvEncUnmapInputResource(m_encoder, resource.mappedResource);
}

Bitstream EncoderNV::lock()
{
	NV_ENC_LOCK_BITSTREAM params = {};
	params.version = NV_ENC_LOCK_BITSTREAM_VER;
	params.outputBitstream = m_outputBufferPtr;
	if(NVFAILED(api.nvEncLockBitstream(m_encoder, &params))) {
		throw std::runtime_error("Failed to lock output bitstream");
	}
	return {reinterpret_cast<char*>(params.bitstreamBufferPtr), params.bitstreamSizeInBytes};
}

void EncoderNV::unlock()
{
	if(NVFAILED(api.nvEncUnlockBitstream(m_encoder, m_outputBufferPtr))) {
		throw std::runtime_error("Failed to unlock output bitstream");
	}
}
	
void EncoderNV::registerSurface(const Surface& surface)
{
	NV_ENC_REGISTER_RESOURCE resource = {};
	resource.version = NV_ENC_REGISTER_RESOURCE_VER;
	resource.bufferFormat = m_bufferFormat;
	resource.resourceType = NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX;
	resource.resourceToRegister = surface.pResource;
	resource.width = surface.width;
	resource.height = surface.height;
	resource.subResourceIndex = 0;

	if(NVFAILED(api.nvEncRegisterResource(m_encoder, &resource))) {
		throw std::runtime_error("Failed to register video surface");
	}

	m_inputBufferPtr = resource.registeredResource;
	m_registeredSurface = surface;
}
	
SurfaceFormat EncoderNV::getInputFormat() const
{
	switch(m_bufferFormat) {
	case NV_ENC_BUFFER_FORMAT_ABGR:
		return SurfaceFormat::ABGR;
	case NV_ENC_BUFFER_FORMAT_ARGB:
		return SurfaceFormat::ARGB;
	case NV_ENC_BUFFER_FORMAT_NV12:
		return SurfaceFormat::NV12;
	}
	return SurfaceFormat::Unknown;
}
	
EncoderNV::EncodeConfig EncoderNV::chooseEncodeConfig(GUID requestedPresetGUID) const
{
	enum RankPriority {
		High   = 10,
		Medium = 5,
		Low    = 1,
	};
	std::multimap<int, EncodeConfig, std::greater<int>> rankedConfigs;
	
	uint32_t numEncodeGUIDs;
	api.nvEncGetEncodeGUIDCount(m_encoder, &numEncodeGUIDs);
	std::vector<GUID> encodeGUIDs(numEncodeGUIDs);
	api.nvEncGetEncodeGUIDs(m_encoder, encodeGUIDs.data(), (uint32_t)encodeGUIDs.size(), &numEncodeGUIDs);

	for(GUID encodeGUID : encodeGUIDs) {
		EncodeConfig config = {encodeGUID};
		int rank = 0;

		uint32_t numInputFormats;
		api.nvEncGetInputFormatCount(m_encoder, encodeGUID, &numInputFormats);
		std::vector<NV_ENC_BUFFER_FORMAT> inputFormats(numInputFormats);
		api.nvEncGetInputFormats(m_encoder, encodeGUID, inputFormats.data(), (uint32_t)inputFormats.size(), &numInputFormats);
		for(const NV_ENC_BUFFER_FORMAT format : inputFormats) {
			// Prefer ARGB or ABGR formats.
			if(format == NV_ENC_BUFFER_FORMAT_ARGB || format == NV_ENC_BUFFER_FORMAT_ABGR) {
				rank += RankPriority::Medium;
				config.format = format;
				break;
			}
			// YUV 4:2:0 is also supported.
			if(format == NV_ENC_BUFFER_FORMAT_NV12) {
				config.format = format;
			}
		}
		if(config.format == 0) {
			// No suitable input format found.
			continue;
		}

		uint32_t numPresets;
		api.nvEncGetEncodePresetCount(m_encoder, encodeGUID, &numPresets);
		std::vector<GUID> presetGUIDs(numPresets);
		api.nvEncGetEncodePresetGUIDs(m_encoder, encodeGUID, presetGUIDs.data(), (uint32_t)presetGUIDs.size(), &numPresets);
		for(GUID presetGUID : presetGUIDs) {
			// Prefer matching preset.
			if(presetGUID == requestedPresetGUID) {
				config.presetGUID = presetGUID;
				rank += RankPriority::High;
				break;
			}
			// Otherwise select default preset, if available.
			if(presetGUID == NV_ENC_PRESET_DEFAULT_GUID) {
				config.presetGUID = presetGUID;
				rank += RankPriority::Low;
			}
		}
		// If no suitable preset found, just select the first one.
		if(config.presetGUID == GUID{0}) {
			config.presetGUID = presetGUIDs[0];
		}

		rankedConfigs.insert(std::pair<int, EncodeConfig>{rank, config});
	}

	if(rankedConfigs.size() == 0) {
		throw std::runtime_error("Could not find a suitable encode configuration");
	}
	return rankedConfigs.begin()->second;
}
