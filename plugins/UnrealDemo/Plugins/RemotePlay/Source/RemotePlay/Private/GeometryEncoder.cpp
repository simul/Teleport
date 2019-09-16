#include "GeometryEncoder.h"

#include <algorithm>

#include "libavstream/common.hpp"

#include "LogMacros.h"

GeometryEncoder::GeometryEncoder()
{ 
}


GeometryEncoder::~GeometryEncoder()
{
}

//Clear a passed vector of UIDs that are believed to have already been sent to the client.
//	outUIDs : Vector of all UIDs of resources that could potentially need to be sent across.
//	req : Object that defines what needs to transfered across.
//Returns the size of the vector after having UIDs of already sent resources removed, and puts the new UIDs in the outUIDs vector.
size_t GetNewUIDs(std::vector<avs::uid> & outUIDs, avs::GeometryRequesterBackendInterface * req)
{
	//Remove uids the requester has.
	for(auto it = outUIDs.begin(); it != outUIDs.end();)
	{
		if(req->HasResource(*it))
		{
			it = outUIDs.erase(it);
		}
		else
		{
			++it;
		}
	}

	return outUIDs.size();
}

unsigned char GeometryEncoder::GALU_code[] = { 0x01,0x00,0x80,0xFF };

avs::Result GeometryEncoder::encode(uint32_t timestamp, avs::GeometrySourceBackendInterface * src, avs::GeometryRequesterBackendInterface * req)
{
	buffer.clear();

	// The source backend will give us the data to encode.
	// What data it provides depends on the contents of the avs::GeometryRequesterBackendInterface object.
	
	std::vector<avs::uid> meshUIDs, textureUIDs, materialUIDs, nodeUIDs;
	req->GetResourcesClientNeeds(meshUIDs, textureUIDs, materialUIDs, nodeUIDs);

	if(GetNewUIDs(meshUIDs, req) != 0)
	{
		encodeMeshes(src, req, meshUIDs);
	}
	
	if(GetNewUIDs(materialUIDs, req) != 0)
	{
		encodeMaterials(src, req, materialUIDs);
	}

	if(GetNewUIDs(textureUIDs, req) != 0)
	{
		size_t previousSize = buffer.size();
		encodeTextures(src, req, textureUIDs);
		UE_LOG(LogRemotePlay, Log, TEXT("Texture Buffer Size: %d"), buffer.size() - previousSize);
	}

	if(GetNewUIDs(nodeUIDs, req) != 0)
	{
		encodeNodes(src, req, nodeUIDs);
	}
	
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
	buffer.push_back(GALU_code[0]);
	buffer.push_back(GALU_code[1]);
	buffer.push_back(GALU_code[2]);
	buffer.push_back(GALU_code[3]);

	put(avs::GeometryPayloadType::Mesh);

	put(missingUIDs.size());

	std::vector<avs::uid> accessors;
	for(size_t i = 0; i < missingUIDs.size(); i++)
	{
		avs::uid uid = missingUIDs[i];
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

		req->EncodedResource(uid);
	}
	put(accessors.size());
	std::vector<avs::uid> bufferViews;
	for(size_t i = 0; i < accessors.size(); i++)
	{
		avs::Accessor accessor;
		src->getAccessor(accessors[i], accessor);
		put(accessors[i]);
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
		put(bufferViews[i]);
		put(bufferView.buffer);
		put(bufferView.byteOffset);
		put(bufferView.byteLength);
		put(bufferView.byteStride);
		buffers.push_back(bufferView.buffer);
	}
	put(buffers.size());
	for(size_t i = 0; i < buffers.size(); i++)
	{
		avs::GeometryBuffer b;
		src->getBuffer(buffers[i], b);
		put(buffers[i]);
		put(b.byteLength);
		put(b.data, b.byteLength);
	}

	return avs::Result::OK;
}

avs::Result GeometryEncoder::encodeNodes(avs::GeometrySourceBackendInterface * src, avs::GeometryRequesterBackendInterface *req, std::vector<avs::uid> missingUIDs)
{
	buffer.push_back(GALU_code[0]);
	buffer.push_back(GALU_code[1]);
	buffer.push_back(GALU_code[2]);
	buffer.push_back(GALU_code[3]);

	put(avs::GeometryPayloadType::Node);

	put(missingUIDs.size());
	for (const avs::uid &uid : missingUIDs) 
	{
		std::shared_ptr<avs::DataNode> node;
		src->getNode(uid, node);

		put(uid);
		auto transform = node->transform;
		avs::ConvertTransform(avs::AxesStandard::UnrealStyle, req->GetAxesStandard(), transform);

		put(transform);
		put(node->data_uid);
		put(node->data_type); 
		put(node->materials.size());
		for (const auto& id : node->materials)
		{
			put(id);
		}
		put(node->childrenUids.size());
		for (const auto& id : node->childrenUids)
		{
			put(id);
		}

		req->EncodedResource(uid);
	}

	return avs::Result::OK;
}

avs::Result GeometryEncoder::encodeTextures(avs::GeometrySourceBackendInterface * src, avs::GeometryRequesterBackendInterface * req, std::vector<avs::uid> missingUIDs)
{
	buffer.push_back(GALU_code[0]);
	buffer.push_back(GALU_code[1]);
	buffer.push_back(GALU_code[2]);
	buffer.push_back(GALU_code[3]);

	//Place payload type onto the buffer.
	put(avs::GeometryPayloadType::Texture);

	//Push amount of textures we are sending.
	put(missingUIDs.size());

	encodeTexturesBackend(src, req, missingUIDs);

	return avs::Result::OK;
}

avs::Result GeometryEncoder::encodeMaterials(avs::GeometrySourceBackendInterface * src, avs::GeometryRequesterBackendInterface * req, std::vector<avs::uid> missingUIDs)
{
	buffer.push_back(GALU_code[0]);
	buffer.push_back(GALU_code[1]);
	buffer.push_back(GALU_code[2]);
	buffer.push_back(GALU_code[3]);

	//Place payload type onto the buffer.
	put(avs::GeometryPayloadType::Material);

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
			put(outMaterial.pbrMetallicRoughness.baseColorTexture.tiling.x);
			put(outMaterial.pbrMetallicRoughness.baseColorTexture.tiling.y);
			put(outMaterial.pbrMetallicRoughness.baseColorFactor.x);
			put(outMaterial.pbrMetallicRoughness.baseColorFactor.y);
			put(outMaterial.pbrMetallicRoughness.baseColorFactor.z);
			put(outMaterial.pbrMetallicRoughness.baseColorFactor.w);

			//Push metallic roughness, and factors.
			put(outMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index);
			put(outMaterial.pbrMetallicRoughness.metallicRoughnessTexture.texCoord);
			put(outMaterial.pbrMetallicRoughness.metallicRoughnessTexture.tiling.x);
			put(outMaterial.pbrMetallicRoughness.metallicRoughnessTexture.tiling.y);
			put(outMaterial.pbrMetallicRoughness.metallicFactor);
			put(outMaterial.pbrMetallicRoughness.roughnessFactor);

			//Push normal map, and scale.
			put(outMaterial.normalTexture.index);
			put(outMaterial.normalTexture.texCoord);
			put(outMaterial.normalTexture.tiling.x);
			put(outMaterial.normalTexture.tiling.y);
			put(outMaterial.normalTexture.scale);

			//Push occlusion texture, and strength.
			put(outMaterial.occlusionTexture.index);
			put(outMaterial.occlusionTexture.texCoord);
			put(outMaterial.occlusionTexture.tiling.x);
			put(outMaterial.occlusionTexture.tiling.y);
			put(outMaterial.occlusionTexture.strength);

			//Push emissive texture, and factor.
			put(outMaterial.emissiveTexture.index);
			put(outMaterial.emissiveTexture.texCoord);
			put(outMaterial.emissiveTexture.tiling.x);
			put(outMaterial.emissiveTexture.tiling.y);
			put(outMaterial.emissiveFactor.x);
			put(outMaterial.emissiveFactor.y);
			put(outMaterial.emissiveFactor.z);

			//Push extension amount.
			put(outMaterial.extensions.size());
			//Push extensions.
			for(const auto &extensionPair : outMaterial.extensions)
			{
				extensionPair.second->serialise(buffer);
			}

			//UIDs used by textures in material.
			std::vector<avs::uid> materialTexture_uids =
			{
				outMaterial.pbrMetallicRoughness.baseColorTexture.index,
				outMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index,
				outMaterial.normalTexture.index,
				outMaterial.occlusionTexture.index,
				outMaterial.emissiveTexture.index
			};

			//Array needs to be sorted for std::unique; we won't have many elements anyway.
			std::sort(materialTexture_uids.begin(), materialTexture_uids.end());
			//Shift data over duplicates, and erase.
			materialTexture_uids.erase(std::unique(materialTexture_uids.begin(), materialTexture_uids.end()), materialTexture_uids.end());
			//Shift data over 0s, and erase.
			materialTexture_uids.erase(std::remove(materialTexture_uids.begin(), materialTexture_uids.end(), 0), materialTexture_uids.end());
			
			//Only send textures that we have not already sent to the client.
			GetNewUIDs(materialTexture_uids, req);

			//Push amount of textures we are sending.
			put(materialTexture_uids.size());
			
			if(materialTexture_uids.size() != 0)
			{
				//Push textures.
				encodeTexturesBackend(src, req, materialTexture_uids);
			}

			//Flag we have encoded the material.
			req->EncodedResource(uid);
		}
	}

	return avs::Result::OK;
}

avs::Result GeometryEncoder::encodeTexturesBackend(avs::GeometrySourceBackendInterface * src, avs::GeometryRequesterBackendInterface * req, std::vector<avs::uid> missingUIDs)
{
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

			//Push size, and data.
			put(outTexture.dataSize);
			put(outTexture.data, outTexture.dataSize);

			//Push sampler identifier.
			put(outTexture.sampler_uid);

			//Flag we have encoded the texture.
			req->EncodedResource(uid);
		}
	}

	return avs::Result::OK;
}
