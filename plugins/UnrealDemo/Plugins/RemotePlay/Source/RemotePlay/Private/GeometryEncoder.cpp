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
	char txt[] = "geometry";
	unsigned char GALU_code[] = { 0x01,0x00,0x80,0xFF };
	buffer.clear();
	buffer.push_back(GALU_code[0]);
	buffer.push_back(GALU_code[1]);
	buffer.push_back(GALU_code[2]);
	buffer.push_back(GALU_code[3]);
#if 1
	// The source backend will give us the data to encode.
	// What data it provides depends on the contents of the avs::GeometryRequesterBackendInterface object.
	size_t num = src->getMeshCount();
	std::vector<avs::uid> missing;
	for (size_t i = 0; i < num; i++)
	{
		avs::uid uid = src->getMeshUid(i);
		if (!req->hasMesh(uid))
			missing.push_back(uid);
	}
	put(missing.size());
	std::vector<avs::uid> accessors;
	for (size_t i = 0; i < missing.size(); i++)
	{
		avs::uid uid = missing[i];
		if (!req->hasMesh(uid))
		{
			put(uid);
			// Requester doesn't have this mesh, and needs it, so we will encode the mesh for transport.
			size_t prims = src->getMeshPrimitiveArrayCount(uid);
			put(prims);
			for (size_t j = 0; j < prims; j++)
			{
				avs::PrimitiveArray primitiveArray;
				src->getMeshPrimitiveArray(uid, j, primitiveArray);
				put(primitiveArray.attributeCount);
				put(primitiveArray.indices_accessor);
				put(primitiveArray.material);
				put(primitiveArray.primitiveMode);
				accessors.push_back( primitiveArray.indices_accessor );
				for (size_t k = 0; k < primitiveArray.attributeCount; k++)
				{
					put(primitiveArray.attributes[k]);
					accessors.push_back(primitiveArray.attributes[k].accessor);
				}
			}
		}
	}
	put(accessors.size());
	std::vector<avs::uid> bufferViews;
	for (size_t i = 0; i < accessors.size(); i++)
	{
		avs::Accessor accessor;
		src->getAccessor(accessors[i], accessor);
		put(accessor.type);
		put(accessor.componentType);
		put(accessor.count);
		put(accessor.bufferView);
		bufferViews.push_back(accessor.bufferView);
		put(accessor.byteOffset);
	}
	put(bufferViews.size());
	std::vector<avs::uid> buffers;
	for (size_t i = 0; i < bufferViews.size(); i++)
	{
		avs::BufferView bufferView;
		src->getBufferView(bufferViews[i], bufferView);
		put(bufferView.buffer);
		buffers.push_back(bufferView.buffer);
		put(bufferView.byteOffset);
		put(bufferView.byteLength);
		put(bufferView.byteStride);
	}
	put(buffers.size());
	for (size_t i = 0; i < buffers.size(); i++)
	{
		avs::GeometryBuffer b;
		src->getBuffer(buffers[i], b);
		put(b.byteLength);
		put(b.data, b.byteLength);
	}
#else
	for (int i = 0; i < strlen(txt); i++)
	{
		buffer.push_back(txt[i]);
	}
#endif

	buffer.push_back(GALU_code[0]);
	buffer.push_back(GALU_code[1]);
	buffer.push_back(GALU_code[2]);
	buffer.push_back(GALU_code[3]);

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

avs::Result GeometryEncoder::encodeTextures(avs::GeometrySourceBackendInterface * src, avs::GeometryRequesterBackendInterface * req)
{
	std::vector<avs::uid> textureUIDs = src->getTextureUIDs();

	//Remove uids the requester has.
	for(auto it = textureUIDs.begin(); it != textureUIDs.end();)
	{
		if(req->hasTexture(*it))
		{
			it = textureUIDs.erase(it);
		}
		else
		{
			++it;
		}
	}

	//Push amount of textures we are sending.
	buffer.push_back(textureUIDs.size());
	for(avs::uid uid : textureUIDs)
	{
		avs::Texture outTexture;

		if(src->getTexture(uid, outTexture))
		{
			//Push identifier.
			buffer.push_back(uid);

			//Push dimensions.
			buffer.push_back(outTexture.width);
			buffer.push_back(outTexture.height);

			//Push bits per pixel.
			buffer.push_back(outTexture.bytesPerPixel);

			//Push size (channels * width * height)
			size_t textureSize = 4 * outTexture.width * outTexture.height;
			buffer.push_back(textureSize);

			//Push pixel data.
			for(int i = 0; i < textureSize; i++)
			{
				buffer.push_back(outTexture.data[i]);
			}
		}
	}

	return avs::Result::OK;
}

avs::Result GeometryEncoder::encodeMaterial(avs::GeometrySourceBackendInterface * src, avs::GeometryRequesterBackendInterface * req)
{
	std::vector<avs::uid> materialUIDs = src->getMaterialUIDs();

	//Remove uids the requester has.
	for(auto it = materialUIDs.begin(); it != materialUIDs.end();)
	{
		if(req->hasMaterial(*it))
		{
			it = materialUIDs.erase(it);
		}
		else
		{
			++it;
		}
	}

	//Push amount of materials.
	buffer.push_back(materialUIDs.size());
	for(avs::uid uid : materialUIDs)
	{
		avs::Material outMaterial;

		if(src->getMaterial(uid, outMaterial))
		{
			//Push identifier.
			buffer.push_back(uid);

			//Push identifiers for textures forming material.
			buffer.push_back(outMaterial.diffuse_uid);
			buffer.push_back(outMaterial.normal_uid);
			buffer.push_back(outMaterial.mro_uid);
		}
	}

	return avs::Result::OK;
}
