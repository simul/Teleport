#include "GeometryEncoder.h"

#include <algorithm>
#include <set>

#include "libavstream/common.hpp"

#include "LogMacros.h"

#include "RemotePlayMonitor.h"

GeometryEncoder::GeometryEncoder()
{}

GeometryEncoder::~GeometryEncoder()
{}

void GeometryEncoder::Initialise(ARemotePlayMonitor* Monitor)
{
	this->Monitor = Monitor;
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
	
	//The buffer will have data put onto it node-by-node until after placing a node onto the buffer there is too much data to put on; causing it to wait until the next encode call.
	///The biggest problem with the current implementation is if the buffer is just below the threshold, and then the next node has a lot of data to push onto it.

	std::vector<avs::MeshNodeResources> meshNodeResources;
	std::vector<avs::LightNodeResources> lightNodeResources;

	req->GetResourcesToStream(meshNodeResources, lightNodeResources);

	//Encode mesh nodes first, as they should be sent before lighting data.
	for(avs::MeshNodeResources meshResourceInfo : meshNodeResources)
	{
		if(!req->HasResource(meshResourceInfo.mesh_uid))
		{
			encodeMeshes(src, req, {meshResourceInfo.mesh_uid});
		}

		for(avs::MaterialResources material : meshResourceInfo.materials)
		{
			if(GetNewUIDs(material.texture_uids, req) != 0)
			{
				encodeTextures(src, req, material.texture_uids);
			}

			if(!req->HasResource(material.material_uid))
			{
				encodeMaterials(src, req, {material.material_uid});
			}
		}

		if(!req->HasResource(meshResourceInfo.node_uid))
		{
			encodeNodes(src, req, {meshResourceInfo.node_uid});
		}

		//Stop encoding actors, if the buffer size is too large.
		if(buffer.size() >= Monitor->GeometryBufferCutoffSize)
		{
			break;
		}
	}

	//Encode light nodes, if there is not too much data from the mesh nodes.
	if(buffer.size() < Monitor->GeometryBufferCutoffSize)
	{
		for(avs::LightNodeResources lightResourceInfo : lightNodeResources)
		{
			if(!req->HasResource(lightResourceInfo.shadowmap_uid))
			{
				encodeTextures(src, req, {lightResourceInfo.shadowmap_uid});
			}

			if(!req->HasResource(lightResourceInfo.node_uid))
			{
				encodeNodes(src, req, {lightResourceInfo.node_uid});
			}

			//Stop encoding light nodes, if the buffer size is too large.
			if(buffer.size() >= Monitor->GeometryBufferCutoffSize)
			{
				break;
			}
		}
	}

	if(buffer.size() > Monitor->GeometryBufferCutoffSize + 10240)
	{
		float cutoffDifference = buffer.size() - Monitor->GeometryBufferCutoffSize;
		UE_LOG(LogRemotePlay, Warning, TEXT("Buffer size was %.2fMB; %.2fMB more than the cutoff(%.2fMB)."), buffer.size() / 1048576.0f, cutoffDifference / 1048576.0f, Monitor->GeometryBufferCutoffSize / 1048576.0f);
	}

	// GALU to end.
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
	for(size_t h = 0; h < missingUIDs.size(); h++)
	{
		size_t oldBufferSize = buffer.size();

		putPayload(avs::GeometryPayloadType::Mesh);
		put((size_t)1);

		avs::uid uid = missingUIDs[h];
		put(uid);

		size_t prims = src->getMeshPrimitiveArrayCount(uid);
		put(prims);
		std::set<avs::uid> accessors;
		for(size_t j = 0; j < prims; j++)
		{
			avs::PrimitiveArray primitiveArray;
			src->getMeshPrimitiveArray(uid, j, primitiveArray);
			put(primitiveArray.attributeCount);
			put(primitiveArray.indices_accessor);
			put(primitiveArray.material);
			put(primitiveArray.primitiveMode);
			accessors.insert(primitiveArray.indices_accessor);
			for(size_t k = 0; k < primitiveArray.attributeCount; k++)
			{
				put(primitiveArray.attributes[k]);
				accessors.insert(primitiveArray.attributes[k].accessor);
			}
		}
		req->EncodedResource(uid);

		put(accessors.size());
		std::set<avs::uid> bufferViews;
		for(avs::uid accessorID : accessors)
		{
			avs::Accessor accessor;
			src->getAccessor(accessorID, accessor);
			put(accessorID);
			put(accessor.type);
			put(accessor.componentType);
			put(accessor.count);
			put(accessor.bufferView);
			bufferViews.insert(accessor.bufferView);
			put(accessor.byteOffset);
		}

		put(bufferViews.size());
		std::set<avs::uid> buffers;
		for(avs::uid bufferViewID : bufferViews)
		{
			avs::BufferView bufferView;
			src->getBufferView(bufferViewID, bufferView);
			put(bufferViewID);
			put(bufferView.buffer);
			put(bufferView.byteOffset);
			put(bufferView.byteLength);
			put(bufferView.byteStride);
			buffers.insert(bufferView.buffer);
		}

		put(buffers.size());
		for(avs::uid bufferID : buffers)
		{
			avs::GeometryBuffer b;
			src->getBuffer(bufferID, b);
			put(bufferID);
			put(b.byteLength);
			put(b.data, b.byteLength);
		}

		float sizeDifference = buffer.size() - oldBufferSize;
		UE_CLOG(sizeDifference > Monitor->GeometryBufferCutoffSize, LogRemotePlay, Warning, TEXT("Mesh(%llu) was encoded as %.2fMB. Cutoff is: %.2fMB"), uid, sizeDifference / 1048576.0f, Monitor->GeometryBufferCutoffSize / 1048576.0f);
	}
	return avs::Result::OK;
}

avs::Result GeometryEncoder::encodeNodes(avs::GeometrySourceBackendInterface * src, avs::GeometryRequesterBackendInterface *req, std::vector<avs::uid> missingUIDs)
{
	//Place payload type onto the buffer.
	putPayload(avs::GeometryPayloadType::Node);

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

void GeometryEncoder::putPayload(avs::GeometryPayloadType t)
{
	buffer.push_back(GALU_code[0]);
	buffer.push_back(GALU_code[1]);
	buffer.push_back(GALU_code[2]);
	buffer.push_back(GALU_code[3]);
	//Place payload type onto the buffer.
	put(t);
}

avs::Result GeometryEncoder::encodeTextures(avs::GeometrySourceBackendInterface * src, avs::GeometryRequesterBackendInterface * req, std::vector<avs::uid> missingUIDs)
{
	encodeTexturesBackend(src, req, missingUIDs);
	return avs::Result::OK;
}

avs::Result GeometryEncoder::encodeMaterials(avs::GeometrySourceBackendInterface * src, avs::GeometryRequesterBackendInterface * req, std::vector<avs::uid> missingUIDs)
{
	//Push amount of materials.
	for(avs::uid uid : missingUIDs)
	{
		avs::Material outMaterial;

		if(src->getMaterial(uid, outMaterial))
		{
			putPayload(avs::GeometryPayloadType::Material);
			put((size_t)1);
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

avs::Result GeometryEncoder::encodeShadowMaps(avs::GeometrySourceBackendInterface* src, avs::GeometryRequesterBackendInterface* req, std::vector<avs::uid> missingUIDs)
{
	encodeTexturesBackend(src, req, missingUIDs, true);
	return avs::Result::OK;
}

avs::Result GeometryEncoder::encodeTexturesBackend(avs::GeometrySourceBackendInterface * src, avs::GeometryRequesterBackendInterface * req, std::vector<avs::uid> missingUIDs, bool isShadowMap)
{
	for(avs::uid uid : missingUIDs)
	{
		avs::Texture outTexture;
		bool textureIsFound = false;
		if(isShadowMap)
			textureIsFound = src->getShadowMap(uid, outTexture);
		else
			textureIsFound = src->getTexture(uid, outTexture);

		if(textureIsFound)
		{
			size_t oldBufferSize = buffer.size();

			//Place payload type onto the buffer.
			putPayload(avs::GeometryPayloadType::Texture);
			//Push amount of textures we are sending.
			put((size_t)1);
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
			put(outTexture.compression);

			//Push size, and data.
			put(outTexture.dataSize);
			put(outTexture.data, outTexture.dataSize);

			//Push sampler identifier.
			put(outTexture.sampler_uid);

			//Flag we have encoded the texture.
			req->EncodedResource(uid);

			float sizeDifference = buffer.size() - oldBufferSize;
			UE_CLOG(sizeDifference > Monitor->GeometryBufferCutoffSize, LogRemotePlay, Warning, TEXT("Texture \"%s\"(%llu) was encoded as %.2fMB. Cutoff is: %.2fMB"), ANSI_TO_TCHAR(outTexture.name.data()), uid, sizeDifference / 1048576.0f, Monitor->GeometryBufferCutoffSize / 1048576.0f);
		}
	}

	return avs::Result::OK;
}
