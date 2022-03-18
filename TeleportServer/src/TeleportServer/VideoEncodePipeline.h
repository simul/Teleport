#pragma once

#include <memory>
#include "CasterContext.h"
#include "ServerSettings.h"

// Forward declare so classes that include don't have to know about them
namespace avs
{
	class Pipeline;
	class Encoder;
	class Surface;
}

namespace teleport
{
	struct VideoEncodeParams
	{
		int32_t encodeWidth = 0;
		int32_t encodeHeight = 0;
		GraphicsDeviceType deviceType;
		void* deviceHandle = nullptr;
		void* inputSurfaceResource = nullptr;
	};

	class VideoEncodePipeline
	{
	public:
		VideoEncodePipeline() = default;
		virtual ~VideoEncodePipeline();

		Result initialize(const ServerSettings& settings, const VideoEncodeParams& videoEncodeParams, avs::PipelineNode* videoOutput, avs::IOInterface* tagDataOutput);
		Result reconfigure(const ServerSettings& settings, const VideoEncodeParams& videoEncodeParams);
		Result process(const uint8_t* tagData, size_t tagDataSize, bool forceIDR = false);
		Result release();

		avs::EncoderStats getEncoderStats() const;

		static Result getEncodeCapabilities(const ServerSettings& settings, const VideoEncodeParams& videoEncodeParams, avs::EncodeCapabilities& capabilities);

	private:
		std::unique_ptr<avs::Pipeline> mPipeline;
		std::unique_ptr<avs::Surface> mInputSurface;
		std::unique_ptr<avs::Encoder> mEncoder;
		avs::IOInterface* mTagDataOutput = nullptr;
	};
}