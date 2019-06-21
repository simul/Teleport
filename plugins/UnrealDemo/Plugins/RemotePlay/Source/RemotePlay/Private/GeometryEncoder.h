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
	template<typename T> size_t put(const T&data)
	{
		size_t pos = buffer.size();
		buffer.resize(buffer.size()+sizeof(T));
		memcpy(buffer.data() + pos,&data,sizeof(T));
		return pos;
	}
	size_t put(const uint8_t *data,size_t count)
	{
		size_t pos = buffer.size();
		buffer.resize(buffer.size() + count);
		memcpy(buffer.data() + pos, data, count);
		return pos;
	}
	template<typename T> void replace(size_t pos,const T&data)
	{
		memcpy(buffer.data() + pos, &data, sizeof(T));
	}
};
