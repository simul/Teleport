#pragma once
#include <libavstream/geometry/mesh_interface.hpp>

class GeometryEncoder : public avs::GeometryEncoderBackendInterface
{
public:
	GeometryEncoder();
	~GeometryEncoder();

	// Inherited via GeometryEncoderBackendInterface
	avs::Result encode(uint32_t timestamp, avs::GeometrySourceBackendInterface * target
		, avs::GeometryRequesterBackendInterface *geometryRequester) override;
	avs::Result mapOutputBuffer(void *& bufferPtr, size_t & bufferSizeInBytes) override;
	avs::Result unmapOutputBuffer() override;
protected:
	std::vector<char> buffer;
};
