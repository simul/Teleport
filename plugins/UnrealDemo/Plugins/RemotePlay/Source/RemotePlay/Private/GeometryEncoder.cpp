#include "GeometryEncoder.h"

GeometryEncoder::GeometryEncoder()
{ 
}


GeometryEncoder::~GeometryEncoder()
{
}

avs::Result GeometryEncoder::encode(uint32_t timestamp
	, avs::GeometrySourceBackendInterface * src
	, avs::GeometryRequesterBackendInterface *req)
{
#if 1
	char txt[] = "geometry";
	unsigned char GALU_code[] = { 0x01,0x00,0x80,0xFF };
	buffer.clear();
	buffer.push_back(GALU_code[0]);
	buffer.push_back(GALU_code[1]);
	buffer.push_back(GALU_code[2]);
	buffer.push_back(GALU_code[3]);
	for (int i = 0; i < strlen(txt); i++)
	{
		buffer.push_back(txt[i]);
	}
	buffer.push_back(GALU_code[0]);
	buffer.push_back(GALU_code[1]);
	buffer.push_back(GALU_code[2]);
	buffer.push_back(GALU_code[3]);
#endif
	// The source backend will give us the data to encode.
	// What data it provides depends on the contents of the avs::GeometryRequesterBackendInterface object.
	size_t num=src->getMeshCount();
	for (size_t i = 0; i < num; i++)
	{
		avs::uid uid = src->getMeshUid(i);
		if (!req->hasMesh(uid))
		{
			// Requester doesn't have this mesh, and needs it, so:
			size_t prims=src->getMeshPrimitiveArrayCount(uid);
			for (size_t j = 0; j < prims; j++)
			{
				avs::PrimitiveArray primitiveArray;
				src->getMeshPrimitiveArray(uid, j, primitiveArray);
				primitiveArray.attributeCount;
			}
		}
	}
	return avs::Result::OK;
}

avs::Result GeometryEncoder::mapOutputBuffer(void *& bufferPtr, size_t & bufferSizeInBytes)
{
	bufferSizeInBytes = buffer.size();
	bufferPtr = buffer.data(); 
	return avs::Result::OK;
}

avs::Result GeometryEncoder::unmapOutputBuffer()
{
	buffer.clear();
	return avs::Result::OK;
}
