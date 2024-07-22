// libavstream
// (c) Copyright 2018-2024 Simul Software Ltd

#include "decoder_p.hpp"
#include "decoders/dec_nvidia.hpp"

#include <libavstream/surface.hpp>
#include <libavstream/surfaces/surface_interface.hpp>
#include <libavstream/stream/parser_interface.hpp>

#include <libavstream/timer.hpp>


using namespace avs;

Decoder::Decoder(DecoderBackend backend)
	: PipelineNode(new Decoder::Private(this))
{
	m_data = (Decoder::Private*)m_d;
	m_selectedBackendType = backend;
	setNumSlots(1, 1);
}

Decoder::Decoder(DecoderBackendInterface* backend)
	: Decoder(DecoderBackend::Custom)
{
	m_data = (Decoder::Private*)m_d;
	m_backend.reset(backend);
}

Decoder::~Decoder()
{
	deconfigure();
}

Result Decoder::configure(const DeviceHandle& device, int frameWidth, int frameHeight, const DecoderParams& params, uint8_t streamId)
{
	name="Decoder";
	m_interimFramesProcessed = 0;
	m_currentFrameNumber = 0;
	m_surfaceRegistered = false;
	m_idrRequired = true;

	m_firstIDRReceived = false;

	if (m_configured)
	{
		Result deconf_result = deconfigure();
		if (deconf_result != Result::OK)
			return Result::Node_AlreadyConfigured;
	}

	auto constructBackendFromType = [](DecoderBackend backendType) -> DecoderBackendInterface*
	{
		// TODO: Detect support for various backends.
		switch (backendType)
		{
#if !defined(PLATFORM_ANDROID)
		case DecoderBackend::NVIDIA:
			return new DecoderNV;
#endif // !PLATFORM_ANDROID
		default:
			return nullptr;
		}
	};
	if (m_backend)
	{
		assert(m_selectedBackendType == DecoderBackend::Custom || m_selectedBackendType == DecoderBackend::Any);
	}
	else
	{
		assert(m_selectedBackendType != DecoderBackend::Custom);

		DecoderBackendInterface* di;
		if (m_selectedBackendType == DecoderBackend::Any)
		{
			di = constructBackendFromType(DecoderBackend::NVIDIA);
			if (!di)
			{
				AVSLOG(Error) << "No suitable decoder backend found\n";
				return Result::Decoder_NoSuitableBackendFound;
			}
		}
		else
		{
			di = constructBackendFromType(m_selectedBackendType);
			if (!di)
			{
				AVSLOG(Error) << "The selected decoder backend is not supported by this system\n";
				return Result::Decoder_NoSuitableBackendFound;
			}
		}
		m_backend.reset(di);
	}

	assert(m_backend);
	Result result = m_backend->initialize(device, frameWidth, frameHeight, params);
	if (!result)
	{
		return result;
	}

	switch (params.codec)
	{
	case VideoCodec::H264:
		m_parser.reset(new NALUParser_H264);
		break;
	case VideoCodec::HEVC:
		m_parser.reset(new NALUParser_H265);
		break;
	default:
		assert(false);
	}

	// Create and configure video parser
	m_vid_parser.reset(StreamParserInterface::Create(StreamParserType::AVC_AnnexB));

	auto onPacketParsed = [](PipelineNode* node, uint32_t inputNodeIndex, const char* buffer, size_t dataSize, size_t dataOffset, bool isLastPayload)->Result
	{
		Decoder* d = static_cast<Decoder*>(node);
		Result result = d->processPayload((const uint8_t*)buffer, dataSize, dataOffset, isLastPayload);
		if (result == Result::DecoderBackend_ReadyToDisplay)
		{
			++d->m_interimFramesProcessed;
			d->m_displayPending = true;
			return Result::OK;
		}
		return result;
	};

	m_vid_parser->configure(this, onPacketParsed, 0);

	m_params = params;
	m_configured = true;
	m_streamId = streamId;
	m_frameBuffer.resize(450000);

	return Result::OK;
}

Result Decoder::reconfigure(int frameWidth, int frameHeight, const DecoderParams& params)
{
	if (!m_configured)
	{
		return Result::Node_NotConfigured;
	}

	m_idrRequired = true;

	assert(m_backend);
	Result result = m_backend->reconfigure(frameWidth, frameHeight, params);
	if (result)
	{
		m_params = params;
		SurfaceInterface* surface = dynamic_cast<SurfaceInterface*>(getOutput(0));
		registerSurface(surface);
	}
	return result;
}

Result Decoder::deconfigure()
{
	if (!m_configured)
	{
		return Result::Node_NotConfigured;
	}

	Result result = Result::OK;
	if (m_backend)
	{
		unlinkOutput();
		result = m_backend->shutdown();
	}

	m_vid_parser.reset();
	m_parser.reset();
	m_frame = {};
	m_state = {};
	m_stats = {};
	m_configured = false;
	m_displayPending = false;
	m_currentFrameNumber = 0;
	m_interimFramesProcessed = 0;
	m_idrRequired = true;

	return result;
}

Result Decoder::process(uint64_t timestamp, uint64_t deltaTime)
{
	if (!m_configured)
	{
		return Result::Node_NotConfigured;
	}

	if (!dynamic_cast<SurfaceInterface*>(getOutput(0)))
	{
		return Result::Node_InvalidOutput;
	}

	IOInterface* input = dynamic_cast<IOInterface*>(getInput(0));
	if (!input)
	{
		return Result::Node_InvalidInput;
	}

	Result result = Result::OK;

	if (m_displayPending)
	{
		DisplayFrame();
	}

	do
	{
		m_firstVCLOffset = 0;

		size_t bufferSize = m_frameBuffer.size();
		size_t bytesRead;
		result = input->read(this, m_frameBuffer.data(), bufferSize, bytesRead);

		if (result == Result::IO_Empty)
		{
			break;
		}

		if (result == Result::IO_Retry)
		{
			m_frameBuffer.resize(bufferSize);
			result = input->read(this, m_frameBuffer.data(), bufferSize, bytesRead);
		}

		if (result != Result::OK || bytesRead < sizeof(StreamPayloadInfo))
		{
			AVSLOG(Warning) << "Decoder: Failed to read input.\n";
			return result;
		}

		// Copy frame info 
		memcpy(&m_frame, m_frameBuffer.data(), sizeof(StreamPayloadInfo));

		++m_stats.framesReceived;

		if (m_frame.connectionTime)
		{
			m_stats.framesReceivedPerSec = float(m_stats.framesReceived / m_frame.connectionTime);
		}

		// Check if data was lost or corrupted
		if (m_frame.broken || m_frame.dataSize == 0)
		{
			if (m_frame.broken)
			{
				AVSLOG(Warning) << "Decoder: frame of size " << m_frame.dataSize << " is broken.\n";
			}
			m_state = {};
			m_idrRequired = true;
			continue;
		}

		// Check if a frame was missed.
		// This frame could happen to be an IDR and the issue might fix itself.
		// First frame will have frameID of 1.
		if (m_frame.frameID - m_currentFrameNumber > 1)
		{
			m_state = {};
			m_idrRequired = true;
		}

		// Parse will call the onPacketParsed callback and this will call processPayload
		// processPayload will call the decoder
		result = m_vid_parser->parse((const char*)(m_frameBuffer.data() + sizeof(StreamPayloadInfo)), m_frame.dataSize);
		// Any decoding for this frame now complete //

		if (!result)
		{
			AVSLOG(Warning) << "Decoder: Failed to parse/decode the video frame \n";
		}
	
		m_currentFrameNumber = m_frame.frameID;
	} while (result == Result::OK);

	if (!m_params.deferDisplay && m_displayPending)
	{
		DisplayFrame();
	}
	
	return result;
}

Result Decoder::DisplayFrame()
{
	m_displayPending = false;
	Result result = m_backend->display(m_showAlphaAsColor);
	if (!result)
	{
		AVSLOG(Error) << "Failed to display video frame.";
	}

	double connection_time_s = TimerUtil::GetElapsedTimeS();
	m_stats.framesProcessed += m_interimFramesProcessed;
	++m_stats.framesDisplayed;
	if (connection_time_s)
	{
		m_stats.framesProcessedPerSec = float(m_stats.framesProcessed / connection_time_s);
		m_stats.framesDisplayedPerSec = float(m_stats.framesDisplayed / connection_time_s);
	}
#if TELEPORT_INTERNAL_CHECKS
	if(m_interimFramesProcessed > 3)
		AVSLOG(Warning) << m_interimFramesProcessed << " interim frames processed \n";
#endif
	m_interimFramesProcessed = 0;
	return result;
}

Result Decoder::setBackend(DecoderBackendInterface* backend)
{
	if (m_configured)
	{
		AVSLOG(Error) << "Decoder: Cannot set backend: already configured";
		return Result::Node_AlreadyConfigured;
	}

	m_selectedBackendType = DecoderBackend::Custom;
	m_backend.reset(backend);
	return Result::OK;
}

Result Decoder::processPayload(const uint8_t* buffer, size_t dataSize, size_t dataOffset, bool isLastPayload)
{
	assert(m_parser);
	assert(m_backend);

	const uint8_t* data = buffer + dataOffset;

	if (dataSize < NALUParserInterface::HeaderSize)
	{
		return Result::Node_InvalidInput;
	}

	Result result = Result::OK;

	VideoPayloadType payloadType = m_parser->classify(data, dataSize);

	// There are two VCLs per frame with alpha layer encoding enabled (HEVC only) and one VCL without.
	if (payloadType == VideoPayloadType::VCL)
	{
		if (!m_firstVCLOffset)
		{
			m_firstVCLOffset = dataOffset;
		}

		bool isIDR = m_parser->isIDR(data, dataSize);

		// Do not process a non-IDR Frame if an IDR is required
		if (m_idrRequired && !isIDR)
		{
			return Result::OK;
		}

		if (!isLastPayload)
		{
			return Result::OK;
		}
	}
	
	bool isCodecConfig = false;
	bool isRedundant = false;

	switch (payloadType)
	{
	case VideoPayloadType::VPS:
		isRedundant = m_state.hasVPS;
		isCodecConfig = true;
		m_state.hasVPS = true;
		break;
	case VideoPayloadType::PPS:
		isRedundant = m_state.hasPPS;
		isCodecConfig = true;
		m_state.hasPPS = true;
		break;
	case VideoPayloadType::SPS:
		isRedundant = m_state.hasSPS;
		isCodecConfig = true;
		m_state.hasSPS = true;
		break;
	case VideoPayloadType::ALE:
		isRedundant = m_state.hasALE;
		isCodecConfig = true;
		m_state.hasALE = true;
		break;
	case VideoPayloadType::VCL:
		// There are two VCLs per frame with alpha layer encoding enabled (HEVC only) and one VCL without.
		if (payloadType == VideoPayloadType::VCL)
		{
			if (!m_firstVCLOffset)
			{
				m_firstVCLOffset = dataOffset;
			}

			bool isIDR = m_parser->isIDR(data, dataSize);

			// Do not process a non-IDR Frame if an IDR is required
			if (m_idrRequired && !isIDR)
			{
				return Result::OK;
			}

			if (!isLastPayload)
			{
				return Result::OK;
			}
		}
		break;
	default:
		break;
	}

	// Do not process if payload is redundant 
	if (isRedundant)
	{
		return Result::OK;
	}

	// Do not process non codec-config payload unless ready to decode VCL packets.
	if (!isCodecConfig && !(m_state.isReady(m_params.codec)))
	{
		return Result::OK;
	}

	if (isLastPayload)
	{
#if defined(PLATFORM_WINDOWS)
		if (m_selectedBackendType == DecoderBackend::Custom)
		{
			// Include ALU. Needed for D3D12 decoder.
			result = m_backend->decode(data - 3, dataSize + 3, nullptr, 0, payloadType, true);
		}
		else
		{
			result = m_backend->decode(buffer, m_frame.dataSize, nullptr, 0, payloadType, true);
		}
#elif defined(PLATFORM_ANDROID)
		// Color is contained in first VCL and alpha in the second.
		if (!m_params.useAlphaLayerDecoding || !m_firstIDRReceived)
        {
			m_firstIDRReceived = true;
            size_t frameSize = m_frame.dataSize - m_firstVCLOffset;
            result = m_backend->decode(buffer + m_firstVCLOffset, frameSize, buffer + m_firstVCLOffset, frameSize, payloadType, true);
        } 
		else
        {
            result = m_backend->decode(buffer + m_firstVCLOffset, dataOffset - m_firstVCLOffset, data, m_frame.dataSize - dataOffset, payloadType, true);
        }
#endif
		m_idrRequired = (result != avs::Result::DecoderBackend_ReadyToDisplay);
	}
	else
	{
#if defined(PLATFORM_WINDOWS)
		if (m_selectedBackendType == DecoderBackend::Custom)
		{
			// Include ALU. Needed for D3D12 decoder.
			result = m_backend->decode(data - 3, dataSize + 3, nullptr, 0, payloadType, false);
		}
#elif defined(PLATFORM_ANDROID)
		result = m_backend->decode(data, dataSize, data, dataSize, payloadType, false);
#endif
	}

	
	return result;
}

Result Decoder::onInputLink(int slot, PipelineNode* node)
{
	if (!dynamic_cast<IOInterface*>(node))
	{
		AVSLOG(Error) << "Decoder: Input node must provide data";
		return Result::Node_Incompatible;
	}
	return Result::OK;
}

Result Decoder::onOutputLink(int slot, PipelineNode* node)
{
	if (!m_configured)
	{
		if(node){
			AVSLOG(Error) << "Decoder: PipelineNode "<<node->name<<" needs to be configured before it can accept output\n";
		}
		return Result::Node_NotConfigured;
	}
	assert(m_backend);

	SurfaceInterface* surface = dynamic_cast<SurfaceInterface*>(node);
	if (!surface)
	{
		AVSLOG(Error) << "Decoder: Output node is not a Surface\n";
		return Result::Node_Incompatible;
	}
	return registerSurface(surface);
}

void Decoder::onOutputUnlink(int slot, PipelineNode* node)
{
	if (!m_configured)
	{
		return;
	}

	SurfaceInterface* surface = dynamic_cast<SurfaceInterface*>(node);
	if (surface)
	{
		unregisterSurface();
	}
}

Result Decoder::registerSurface(SurfaceInterface* surface)
{
	if (m_surfaceRegistered)
	{
		AVSLOG(Error) << "Decoder: Cannot register surface: a surface is already registered";
		return Result::Decoder_SurfaceAlreadyRegistered;
	}

	assert(m_backend);
	Result result = m_backend->registerSurface(surface->getBackendSurface(), surface->getAlphaBackendSurface());

	if (result)
	{
		m_surfaceRegistered = true;
	}

	return result;
}

Result Decoder::unregisterSurface()
{
	if (!m_configured)
	{
		AVSLOG(Error) << "Decoder: Cannot unregister surface: encoder not configured";
		return Result::Node_AlreadyConfigured;
	}

	if (!m_surfaceRegistered)
	{
		AVSLOG(Error) << "Decoder: Cannot unregister surface: no surface is registered";
		return Result::Decoder_SurfaceNotRegistered;
	}

	assert(m_backend);
	Result result = m_backend->unregisterSurface();

	if (result)
	{
		m_surfaceRegistered = false;
	}

	return result;
}

bool Decoder::idrRequired() const
{
	return m_idrRequired;
}

uint8_t Decoder::getStreamId() const
{
	return m_streamId;
}