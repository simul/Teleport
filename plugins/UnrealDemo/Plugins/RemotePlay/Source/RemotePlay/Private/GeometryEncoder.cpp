#include "GeometryEncoder.h"

#include "libavstream/common.hpp"

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
#if 1
	// The source backend will give us the data to encode.
	// What data it provides depends on the contents of the avs::GeometryRequesterBackendInterface object.
	size_t num = src->getMeshCount();
	std::vector<avs::uid> meshUIDs;
	for(size_t i = 0; i < num; i++)
	{
		avs::uid uid = src->getMeshUid(i);
		if(!req->hasMesh(uid))
			meshUIDs.push_back(uid);
	}

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

	std::vector<avs::uid> textureUIDs = src->getTextureUIDs();

	//Remove texture uids the requester has.
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

	if(meshUIDs.size() != 0)
	{
		buffer.push_back(GALU_code[0]);
		buffer.push_back(GALU_code[1]);
		buffer.push_back(GALU_code[2]);
		buffer.push_back(GALU_code[3]);

		//Place payload type onto the buffer.
		put(avs::GeometryPayloadType::Mesh);

		encodeMeshes(src, req, meshUIDs);
	}

	if(materialUIDs.size() != 0)
	{
		buffer.push_back(GALU_code[0]);
		buffer.push_back(GALU_code[1]);
		buffer.push_back(GALU_code[2]);
		buffer.push_back(GALU_code[3]);

		//Place payload type onto the buffer.
		put(avs::GeometryPayloadType::Material);

		encodeMaterials(src, req, materialUIDs);
	}

	///Causes major slowdown while we always determine we need to send the payloads every frame.
/*
	if(textureUIDs.size() != 0)
	{
		buffer.push_back(GALU_code[0]);
		buffer.push_back(GALU_code[1]);
		buffer.push_back(GALU_code[2]);
		buffer.push_back(GALU_code[3]);

		//Place payload type onto the buffer.
		put(avs::GeometryPayloadType::Texture);

		encodeTextures(src, req, textureUIDs);
	}
*/

#else
	buffer.push_back(GALU_code[0]);
	buffer.push_back(GALU_code[1]);
	buffer.push_back(GALU_code[2]);
	buffer.push_back(GALU_code[3]);

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

avs::Result GeometryEncoder::encodeMeshes(avs::GeometrySourceBackendInterface * src, avs::GeometryRequesterBackendInterface * req, std::vector<avs::uid> missingUIDs)
{
	put(missingUIDs.size());
	std::vector<avs::uid> accessors;
	for(size_t i = 0; i < missingUIDs.size(); i++)
	{
		avs::uid uid = missingUIDs[i];
		if(!req->hasMesh(uid))
		{
			put(uid);
			// Requester doesn't have this mesh, and needs it, so we will encode the mesh for transport.
			size_t prims = src->getMeshPrimitiveArrayCount(uid);
			put(prims);
			for(size_t j = 0; j < prims; j++)
			{
				avs::PrimitiveArray primitiveArray;
				src->getMeshPrimitiveArray(uid, j, primitiveArray);
				put(primitiveArray.attributeCount);
				put(primitiveArray.indices_accessor);
				put(primitiveArray.material);
				put(primitiveArray.primitiveMode);
				accessors.push_back(primitiveArray.indices_accessor);
				for(size_t k = 0; k < primitiveArray.attributeCount; k++)
				{
					put(primitiveArray.attributes[k]);
					accessors.push_back(primitiveArray.attributes[k].accessor);
				}
			}
		}
	}
	put(accessors.size());
	std::vector<avs::uid> bufferViews;
	for(size_t i = 0; i < accessors.size(); i++)
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
	for(size_t i = 0; i < bufferViews.size(); i++)
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
	for(size_t i = 0; i < buffers.size(); i++)
	{
		avs::GeometryBuffer b;
		src->getBuffer(buffers[i], b);
		put(b.byteLength);
		put(b.data, b.byteLength);
	}

	return avs::Result::OK;
}

avs::Result GeometryEncoder::encodeTextures(avs::GeometrySourceBackendInterface * src, avs::GeometryRequesterBackendInterface * req, std::vector<avs::uid> missingUIDs)
{
	//Push amount of textures we are sending.
	put(missingUIDs.size());
	for(avs::uid uid : missingUIDs)
	{
		avs::Texture outTexture;

		if(src->getTexture(uid, outTexture))
		{
			//Push identifier.
			put(uid);

			size_t nameLength = outTexture.name.length();

			//Push name length.
			put(nameLength);
			//Push name.
			put((uint8_t*)outTexture.name.data(), nameLength);

			//Push dimensions.
			put(outTexture.width);
			put(outTexture.height);

			//Push additional information.
			put(outTexture.depth);
			put(outTexture.bytesPerPixel);
			put(outTexture.arrayCount);
			put(outTexture.mipCount);

			//Push format.
			put(outTexture.format);

			//Push size (channels * width * height)
			size_t textureSize = 4 * outTexture.width * outTexture.height;
			put(textureSize);

			//Push pixel data.
			put(outTexture.data, textureSize);

			//Push sampler identifier.
			put(outTexture.sampler_uid);
		}
	}

	return avs::Result::OK;
}

avs::Result GeometryEncoder::encodeMaterials(avs::GeometrySourceBackendInterface * src, avs::GeometryRequesterBackendInterface * req, std::vector<avs::uid> missingUIDs)
{
	//Push amount of materials.
	put(missingUIDs.size());
	for(avs::uid uid : missingUIDs)
	{
		avs::Material outMaterial;

		if(src->getMaterial(uid, outMaterial))
		{
			//Push identifier.
			put(uid);

			size_t nameLength = outMaterial.name.length();

			//Push name length.
			put(nameLength);
			//Push name.
			put((uint8_t*)outMaterial.name.data(), nameLength);

			//Push base colour, and factor.
			put(outMaterial.pbrMetallicRoughness.baseColorTexture.index);
			put(outMaterial.pbrMetallicRoughness.baseColorTexture.texCoord);
			put(outMaterial.pbrMetallicRoughness.baseColorFactor.x);
			put(outMaterial.pbrMetallicRoughness.baseColorFactor.y);
			put(outMaterial.pbrMetallicRoughness.baseColorFactor.z);
			put(outMaterial.pbrMetallicRoughness.baseColorFactor.w);

			//Push metallic roughness, and factors.
			put(outMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index);
			put(outMaterial.pbrMetallicRoughness.metallicRoughnessTexture.texCoord);
			put(outMaterial.pbrMetallicRoughness.metallicFactor);
			put(outMaterial.pbrMetallicRoughness.roughnessFactor);

			//Push normal map, and scale.
			put(outMaterial.normalTexture.index);
			put(outMaterial.normalTexture.texCoord);
			put(outMaterial.normalTexture.scale);

			//Push occlusion texture, and strength.
			put(outMaterial.occlusionTexture.index);
			put(outMaterial.occlusionTexture.texCoord);
			put(outMaterial.occlusionTexture.strength);

			//Push emissive texture, and factor.
			put(outMaterial.emissiveTexture.index);
			put(outMaterial.emissiveTexture.texCoord);
			put(outMaterial.emissiveFactor.x);
			put(outMaterial.emissiveFactor.y);
			put(outMaterial.emissiveFactor.z);
		}
	}

	return avs::Result::OK;
}
