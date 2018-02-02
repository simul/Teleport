#include <stdexcept>
#include <vector>
#include <map>
#include <functional>

#include "EncoderNV.hpp"

#define NVFAILED(x) ((x) != NV_ENC_SUCCESS)

EncoderNV::EncoderNV()
	: api({NV_ENCODE_API_FUNCTION_LIST_VER})
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

void EncoderNV::initialize(RendererDevice* device)
{
	NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS params = {};
	params.apiVersion = NVENCAPI_VERSION;
	params.version    = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
	params.device     = reinterpret_cast<void*>(device);
	// Assume DirectX device for now.
	params.deviceType = NV_ENC_DEVICE_TYPE::NV_ENC_DEVICE_TYPE_DIRECTX;
	if(NVFAILED(api.nvEncOpenEncodeSessionEx(&params, &m_encoder))) {
		throw std::runtime_error("Failed to open NVENC encode session");
	}

	EncodeConfig config = chooseEncodeConfig(NV_ENC_PRESET_LOW_LATENCY_HQ_GUID);
}

void EncoderNV::shutdown()
{
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
