#include "GeometryEncoder.h"

#include <algorithm>
#include <set>

#include "libavstream/common.hpp"
#include "libavstream/geometry/animation_interface.h"

#include "ServerSettings.h"

#include "ErrorHandling.h"


//Clear a passed vector of UIDs that are believed to have already been sent to the client.
//	outUIDs : Vector of all UIDs of resources that could potentially need to be sent across.
//	req : Object that defines what needs to transfered across.
//Returns the size of the vector after having UIDs of already sent resources removed, and puts the new UIDs in the outUIDs vector.
size_t GetNewUIDs(std::vector<avs::uid> & outUIDs, avs::GeometryRequesterBackendInterface * req)
{
	//Remove uids the requester has.
	for(auto it = outUIDs.begin(); it != outUIDs.end();)
	{
		if(req->hasResource(*it))
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

namespace teleport
{
	GeometryEncoder::GeometryEncoder(const ServerSettings* settings)
		:settings(settings), prevBufferSize(0)
	{}

	avs::Result GeometryEncoder::encode(uint32_t timestamp, avs::GeometrySourceBackendInterface* src, avs::GeometryRequesterBackendInterface* req)
	{
		if(!req||req->getClientAxesStandard()==avs::AxesStandard::NotInitialized)
			return avs::Result::Failed;
		queuedBuffer.clear();

		// The source backend will give us the data to encode.
		// What data it provides depends on the contents of the avs::GeometryRequesterBackendInterface object.

		//Encode data onto buffer, and then move it onto queuedBuffer.
		//Unless queueing the data would causes queuedBuffer to exceed the recommended buffer size, which will cause the data to stay in buffer until the next encode call.
		//Data may still be queued, and exceed the recommeneded size, if not queueing the data may leave it empty.

		//Queue what may have been left since last time, and keep queueing if there is still some space.
		bool keepQueueing = attemptQueueData();
		if(keepQueueing)
		{
			std::vector<avs::uid> nodeIDsToStream;
			std::vector<avs::MeshNodeResources> meshNodeResources;
			std::vector<avs::LightNodeResources> lightNodeResources;
			std::set<avs::uid> genericTexturesToStream;

			req->getResourcesToStream(nodeIDsToStream, meshNodeResources, lightNodeResources, genericTexturesToStream, minimumPriority);

			for(avs::uid nodeID : nodeIDsToStream)
			{
				if(!req->hasResource(nodeID))
				{
					encodeNodes(src, req, {nodeID});

					keepQueueing = attemptQueueData();
					if(!keepQueueing)
					{
						break;
					}
				}
			}

			//Encode mesh nodes first, as they should be sent before lighting data.
			for(avs::MeshNodeResources meshResourceInfo : meshNodeResources)
			{
				if(!req->hasResource(meshResourceInfo.mesh_uid))
				{
					encodeMeshes(src, req, {meshResourceInfo.mesh_uid});

					keepQueueing = attemptQueueData();
					if(!keepQueueing)
					{
						break;
					}
				}
				if(meshResourceInfo.skinID != 0)
				{
					if( meshResourceInfo.boneIDs.size())
					{
						// It should have either none or all of the joints.
						if(!req->hasResource(meshResourceInfo.boneIDs[0]))
						{
							encodeNodes(src, req, meshResourceInfo.boneIDs);

							keepQueueing = attemptQueueData();
							if(!keepQueueing)
							{
								break;
							}
						}
					}
					if(!req->hasResource(meshResourceInfo.skinID))
					{
						encodeSkin(src, req, meshResourceInfo.skinID);

						keepQueueing = attemptQueueData();
						if(!keepQueueing)
						{
							break;
						}
					}
				}

				for(avs::uid animationID : meshResourceInfo.animationIDs)
				{
					if(!req->hasResource(animationID))
					{
						encodeAnimation(src, req, animationID);

						keepQueueing = attemptQueueData();
						if(!keepQueueing)
						{
							break;
						}
					}
				}
				if(!keepQueueing)
				{
					break;
				}

				for(avs::MaterialResources material : meshResourceInfo.materials)
				{
					if (!req->hasResource(material.material_uid))
					{
						encodeMaterials(src, req, { material.material_uid });
						keepQueueing = attemptQueueData();
						if (!keepQueueing)
						{
							break;
						}
					}
					if(GetNewUIDs(material.texture_uids, req) != 0)
					{
						for(avs::uid textureID : material.texture_uids)
						{
							encodeTextures(src, req, {textureID});
							keepQueueing = attemptQueueData();
							if(!keepQueueing)
							{
								break;
							}
						}
						if(!keepQueueing)
						{
							break;
						}
					}
				}
				if(!keepQueueing)
				{
					break;
				}

				if(!req->hasResource(meshResourceInfo.node_uid))
				{
					encodeNodes(src, req, {meshResourceInfo.node_uid});

					keepQueueing = attemptQueueData();
					if(!keepQueueing)
					{
						break;
					}
				}
			}

			for(avs::LightNodeResources lightResourceInfo : lightNodeResources)
			{
				if(lightResourceInfo.shadowmap_uid)
				{
					if(!req->hasResource(lightResourceInfo.shadowmap_uid))
					{
						encodeTextures(src, req, {lightResourceInfo.shadowmap_uid});

						keepQueueing = attemptQueueData();
						if(!keepQueueing)
						{
							break;
						}
					}
				}

				if(!req->hasResource(lightResourceInfo.node_uid))
				{
					encodeNodes(src, req, {lightResourceInfo.node_uid});

					keepQueueing = attemptQueueData();
					if(!keepQueueing)
					{
						break;
					}
				}
			}

			//Encode mesh nodes first, as they should be sent before lighting data.
			for(avs::uid texture_uid : genericTexturesToStream)
			{
				if(req->hasResource(texture_uid))
					continue;
				encodeTextures(src, req, {texture_uid});
				keepQueueing = attemptQueueData();
				if(!keepQueueing)
				{
					break;
				}
			}
		}

		return avs::Result::OK;
	}

	avs::Result GeometryEncoder::mapOutputBuffer(void *& bufferPtr, size_t & bufferSizeInBytes)
	{
		bufferSizeInBytes = queuedBuffer.size();
		bufferPtr = queuedBuffer.data();
		return avs::Result::OK;
	}

	avs::Result GeometryEncoder::unmapOutputBuffer()
	{
		queuedBuffer.clear();
		return avs::Result::OK;
	}

	void GeometryEncoder::setMinimumPriority(int32_t p)
	{
		minimumPriority=p;
	}

	avs::Result GeometryEncoder::encodeMeshes(avs::GeometrySourceBackendInterface* src, avs::GeometryRequesterBackendInterface* req, std::vector<avs::uid> missingUIDs)
	{
		for(avs::uid uid : missingUIDs)
		{
			size_t oldBufferSize = buffer.size();
			avs::Mesh* mesh = src->getMesh(uid, req->getClientAxesStandard());
			if (!mesh)
			{
				TELEPORT_CERR << "Mesh encoding error! Mesh " << uid << " does not exist!\n";
				continue;
			}
			const avs::CompressedMesh* compressedMesh = src->getCompressedMesh(uid, req->getClientAxesStandard());
			putPayload(avs::GeometryPayloadType::Mesh);
			put((size_t)1);
			put(uid);
			if(compressedMesh&& compressedMesh->meshCompressionType!= avs::MeshCompressionType::NONE)
			{
				put(compressedMesh->meshCompressionType);
				//Push name.
				size_t nameLength = compressedMesh->name.length();
				put(nameLength);
				put((uint8_t*)compressedMesh->name.data(), nameLength);
				//put(compressedMesh->subMeshAttributeIndex);
				size_t num_elements= compressedMesh->subMeshes.size();
				put((uint32_t)num_elements);
				for(size_t i=0;i< num_elements;i++)
				{
					auto &subMesh= compressedMesh->subMeshes[i];
					put(subMesh.indices_accessor);
					put(subMesh.material);
					put(subMesh.first_index);
					put(subMesh.num_indices);
					size_t numAttrs = subMesh.attributeSemantics.size();
					put(numAttrs);
					for (auto a: subMesh.attributeSemantics)
					{
						put((int32_t)a.first);
						put((uint8_t)a.second);
					}
					size_t bufferSize = subMesh.buffer.size();
					put(bufferSize);
					put((uint8_t*)subMesh.buffer.data(), bufferSize);
				}
			}
			else
			{
				put(avs::MeshCompressionType::NONE);
				//Push name length.
				size_t nameLength = mesh->name.length();
				put(nameLength);
				//Push name.
				put((uint8_t*)mesh->name.data(), nameLength);

				put(mesh->primitiveArrays.size());

				std::set<avs::uid> accessors;
				for(const avs::PrimitiveArray& primitiveArray : mesh->primitiveArrays)
				{
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

				put(accessors.size());
				std::set<avs::uid> bufferViews;
				for(avs::uid accessorID : accessors)
				{
					avs::Accessor accessor = mesh->accessors[accessorID];
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
					avs::BufferView bufferView = mesh->bufferViews[bufferViewID];
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
					avs::GeometryBuffer buffer = mesh->buffers[bufferID];
					put(bufferID);
					put(buffer.byteLength);
					put(buffer.data, buffer.byteLength);
				}
			}

			//TELEPORT_COUT<<"Encoded mesh "<<mesh->name.c_str()<<" with size "<<MemSize(buffer.size()-oldBufferSize)<<"\n";
			// Actual size is now known so update payload size
			putPayloadSize();

			req->encodedResource(uid);
		}
		return avs::Result::OK;
	}

	avs::Result GeometryEncoder::encodeNodes(avs::GeometrySourceBackendInterface * src, avs::GeometryRequesterBackendInterface *req, std::vector<avs::uid> missingUIDs)
	{
		//Place payload type onto the buffer.
		putPayload(avs::GeometryPayloadType::Node);
		for (int i=0;i<missingUIDs.size();i++)
		{
			avs::uid uid =missingUIDs[i];
			avs::Node* node = src->getNode(uid);
			if(!node)
			{
				TELEPORT_CERR << "PipelineNode encoding error! Node_" << uid << " does not exist!\n";
				missingUIDs.erase(missingUIDs.begin()+i);
				i--;
			}
		}
		put(missingUIDs.size());
		for (const avs::uid &uid : missingUIDs)
		{
			avs::Node* node = src->getNode(uid);
			put(uid);

			//Push name length.
			size_t nameLength = node->name.length();
			put(nameLength);
			//Push name.
			put((uint8_t*)node->name.data(), nameLength);

			avs::Transform localTransform = node->localTransform;
			avs::Transform globalTransform = node->globalTransform;
			avs::ConvertTransform(settings->serverAxesStandard, req->getClientAxesStandard(), localTransform);
			avs::ConvertTransform(settings->serverAxesStandard, req->getClientAxesStandard(), globalTransform);

			put(localTransform);
			put(globalTransform);
			// If the node is stationary, we will normally use the global transform.
			put((uint8_t)(!node->stationary));
			put((uint8_t)(node->stationary));

			put(node->holder_client_id);

			put(node->priority);
			put(node->data_uid);
			put(node->data_type);

			put(node->skinID);
			put(node->parentID);

			put(node->animations.size());
			for(avs::uid id : node->animations)
			{
				put(id);
			}
			// If the node's priority is less than the *client's* minimum, we don't want
			// to send its mesh.
			if(node->data_type == avs::NodeDataType::Mesh)
			{
				put(node->materials.size());
				for(avs::uid id : node->materials)
				{
					put(id);
				}
				put(node->renderState.lightmapScaleOffset);
				put(node->renderState.globalIlluminationUid);
			}
			else
			{
				TELEPORT_ASSERT(node->materials.size() == 0);
			}

			if(node->data_type==avs::NodeDataType::Light)
			{
				put(node->lightColour);
				put(node->lightRadius);
				put(node->lightRange);
				avs::vec3 lightDirection = node->lightDirection;
				avs::ConvertPosition(settings->serverAxesStandard, req->getClientAxesStandard(), lightDirection);
				put(lightDirection);
				put(node->lightType);
			}
			put(node->childrenIDs.size());
			for(avs::uid id : node->childrenIDs)
			{
				put(id);
			}

			req->encodedResource(uid);
		}

		// Actual size is now known so update payload size
		putPayloadSize();

		return avs::Result::OK;
	}

	avs::Result GeometryEncoder::encodeSkin(avs::GeometrySourceBackendInterface* src, avs::GeometryRequesterBackendInterface* req, avs::uid skinID)
	{
		putPayload(avs::GeometryPayloadType::Skin);

		const avs::Skin* skin = src->getSkin(skinID, req->getClientAxesStandard());
		if(skin)
		{
			put(skinID);

			//Push name length.
			size_t nameLength = skin->name.length();
			put(nameLength);
			//Push name.
			put((uint8_t*)skin->name.data(), nameLength);

			put(skin->inverseBindMatrices.size());
			for(int i = 0; i < skin->inverseBindMatrices.size(); i++)
			{
				put(skin->inverseBindMatrices[i]);
			}
			// TODO: This is inefficient, most boneID's will be jointID's :
			put(skin->boneIDs.size());
			for (int i = 0; i < skin->boneIDs.size(); i++)
			{
				put(skin->boneIDs[i]);
			}

			put(skin->jointIDs.size());
			for(int i = 0; i < skin->jointIDs.size(); i++)
			{
				put(skin->jointIDs[i]);
			}

			put(skin->skinTransform);

			putPayloadSize();
			req->encodedResource(skinID);
		}

		return avs::Result::OK;
	}

	avs::Result GeometryEncoder::encodeAnimation(avs::GeometrySourceBackendInterface* src, avs::GeometryRequesterBackendInterface* req, avs::uid animationID)
	{
		const avs::Animation* animation = src->getAnimation(animationID, req->getClientAxesStandard());
		if(animation)
		{
			putPayload(avs::GeometryPayloadType::Animation);
			put(animationID);

			//Push name length.
			size_t nameLength = animation->name.length();
			put(nameLength);
			//Push name.
			put((uint8_t*)animation->name.data(), nameLength);

			put(animation->boneKeyframes.size());
			for(const avs::TransformKeyframeList& transformKeyframe : animation->boneKeyframes)
			{
				put(transformKeyframe.boneIndex);

				encodeVector3Keyframes(transformKeyframe.positionKeyframes);
				encodeVector4Keyframes(transformKeyframe.rotationKeyframes);
			}

			putPayloadSize();
			req->encodedResource(animationID);
		}

		return avs::Result::OK;
	}

	void GeometryEncoder::putPayload(avs::GeometryPayloadType t)
	{
		prevBufferSize = buffer.size();

		// Add placeholder for the payload size 
		put(size_t(sizeof(avs::GeometryPayloadType)));

		// Place payload type onto the buffer.
		put(t);
	}

	void GeometryEncoder::putPayloadSize()
	{
		if (!buffer.size())
		{
			prevBufferSize = 0;
			return;
		}

		size_t payloadSize = buffer.size() - prevBufferSize - sizeof(size_t);

		// prevBufferSize will be the index where the payload size placeholder was added
		replace(prevBufferSize, payloadSize);

		prevBufferSize = 0;
	}

	avs::Result GeometryEncoder::encodeTextures(avs::GeometrySourceBackendInterface * src, avs::GeometryRequesterBackendInterface * req, std::vector<avs::uid> missingUIDs)
	{
		encodeTexturesBackend(src, req, missingUIDs);
		return avs::Result::OK;
	}

	avs::Result GeometryEncoder::encodeMaterials(avs::GeometrySourceBackendInterface * src, avs::GeometryRequesterBackendInterface * req, std::vector<avs::uid> missingUIDs)
	{
		auto renderingFeatures=req->getClientRenderingFeatures();
		//Push amount of materials.
		for (avs::uid uid : missingUIDs)
		{
			avs::Material* material = src->getMaterial(uid);

			if (material)
			{
				//UIDs used by textures in material.
				std::vector<avs::uid> materialTexture_uids =material ->GetTextureUids();
				for(auto u:materialTexture_uids)
				{
					if(!src->getTexture(u))
					{
						TELEPORT_CERR<<"Material "<<material->name.c_str()<<" points to "<<u<<" which is not a texture."<<std::endl;
						continue;
					}
				}
				putPayload(avs::GeometryPayloadType::Material);
				put((size_t)1);
				put(uid);

				size_t nameLength = material->name.length();

				//Push name length.
				put(nameLength);
				//Push name.
				put((uint8_t*)material->name.data(), nameLength);

				//Push base colour, and factor.
				put(material->pbrMetallicRoughness.baseColorTexture.index);
				put(material->pbrMetallicRoughness.baseColorTexture.texCoord);
				put(material->pbrMetallicRoughness.baseColorTexture.tiling.x);
				put(material->pbrMetallicRoughness.baseColorTexture.tiling.y);
				put(material->pbrMetallicRoughness.baseColorFactor.x);
				put(material->pbrMetallicRoughness.baseColorFactor.y);
				put(material->pbrMetallicRoughness.baseColorFactor.z);
				put(material->pbrMetallicRoughness.baseColorFactor.w);

				//Push metallic roughness, and factors.
				put(material->pbrMetallicRoughness.metallicRoughnessTexture.index);
				put(material->pbrMetallicRoughness.metallicRoughnessTexture.texCoord);
				put(material->pbrMetallicRoughness.metallicRoughnessTexture.tiling.x);
				put(material->pbrMetallicRoughness.metallicRoughnessTexture.tiling.y);
				put(material->pbrMetallicRoughness.metallicFactor);
				put(material->pbrMetallicRoughness.roughnessMultiplier);
				put(material->pbrMetallicRoughness.roughnessOffset);
				
				//Push normal map, and scale.
				// TODO Note: correspondence between these handshake feature checks and those at GeometryStreamingService::GetMeshNodeResources!
				if(renderingFeatures.normals)
					put(material->normalTexture.index);
				else
					put(avs::uid(0));
				put(material->normalTexture.texCoord);
				put(material->normalTexture.tiling.x);
				put(material->normalTexture.tiling.y);
				put(material->normalTexture.scale);

				//Push occlusion texture, and strength.
				// Note, if AO is not supported, we MUST put a zero, or the client will request a missing texture that never arrives.
				if (renderingFeatures.ambientOcclusion)
					put(material->occlusionTexture.index);
				else
					put(avs::uid(0));
				put(material->occlusionTexture.texCoord);
				put(material->occlusionTexture.tiling.x);
				put(material->occlusionTexture.tiling.y);
				put(material->occlusionTexture.strength);

				//Push emissive texture, and factor.
				put(material->emissiveTexture.index);
				put(material->emissiveTexture.texCoord);
				put(material->emissiveTexture.tiling.x);
				put(material->emissiveTexture.tiling.y);
				put(material->emissiveFactor.x);
				put(material->emissiveFactor.y);
				put(material->emissiveFactor.z);

				//Push extension amount.
				put(material->extensions.size());
				//Push extensions.
				for (const auto &extensionPair : material->extensions)
				{
					extensionPair.second->serialise(buffer);
				}


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

				// Actual size is now known so update payload size
				putPayloadSize();

				if (materialTexture_uids.size() != 0)
				{
					//Push textures.
					encodeTexturesBackend(src, req, materialTexture_uids);
				}

				//Flag we have encoded the material.
				req->encodedResource(uid);
			}
		}

		return avs::Result::OK;
	}

	avs::Result GeometryEncoder::encodeShadowMaps(avs::GeometrySourceBackendInterface* src, avs::GeometryRequesterBackendInterface* req, std::vector<avs::uid> missingUIDs)
	{
		encodeTexturesBackend(src, req, missingUIDs, true);
		return avs::Result::OK;
	}

	avs::Result GeometryEncoder::encodeTexturesBackend(avs::GeometrySourceBackendInterface * src, avs::GeometryRequesterBackendInterface * req, std::vector<avs::uid> missingUIDs, bool )
	{
		for (avs::uid uid : missingUIDs)
		{
			avs::Texture* texture;

			texture = src->getTexture(uid);
			if (texture)
			{
				if (texture->compression == avs::TextureCompression::UNCOMPRESSED)
				{
					TELEPORT_CERR << "Trying to send uncompressed texture. Never do this!\n";
					continue;
				}
				size_t oldBufferSize = buffer.size();

				//Place payload type onto the buffer.
				putPayload(avs::GeometryPayloadType::Texture);
				//Push amount of textures we are sending.
				put((size_t)1);
				//Push identifier.
				put(uid);

				size_t nameLength = texture->name.length();

				//Push name length.
				put(nameLength);
				//Push name.
				put((uint8_t*)texture->name.data(), nameLength);

				//Push dimensions.
				put(texture->width);
				put(texture->height);

				//Push additional information.
				put(texture->depth);
				put(texture->bytesPerPixel);
				put(texture->arrayCount);
				put(texture->mipCount);

				//Push format.
				put(texture->format);
				put(texture->compression);

				//Value scale - brightness number to scale the final texel by.
				put(texture->valueScale);

				//Push size, and data.
				put(texture->dataSize);
				put(texture->data, texture->dataSize);

				//Push sampler identifier.
				put(texture->sampler_uid);

				// Actual size is now known so update payload size
				putPayloadSize();

				//Flag we have encoded the texture.
				req->encodedResource(uid);
			}
			else
			{
				//DEBUG_BREAK_ONCE("Missing texture");
				TELEPORT_CERR<<"Trying to encode texture "<<uid<<" but it is not there.\n";
			}
		}

		return avs::Result::OK;
	}

	avs::Result GeometryEncoder::encodeFloatKeyframes(const std::vector<avs::FloatKeyframe>& keyframes)
	{
		put(keyframes.size());
		for(const avs::FloatKeyframe& keyframe : keyframes)
		{
			put(keyframe.time);
			put(keyframe.value);
		}

		return avs::Result::OK;
	}

	avs::Result GeometryEncoder::encodeVector3Keyframes(const std::vector<avs::Vector3Keyframe>& keyframes)
	{
		put(keyframes.size());
		for(const avs::Vector3Keyframe& keyframe : keyframes)
		{
			put(keyframe.time);
			put(keyframe.value);
		}

		return avs::Result::OK;
	}

	avs::Result GeometryEncoder::encodeVector4Keyframes(const std::vector<avs::Vector4Keyframe>& keyframes)
	{
		put(keyframes.size());
		for(const avs::Vector4Keyframe& keyframe : keyframes)
		{
			put(keyframe.time);
			put(keyframe.value);
		}

		return avs::Result::OK;
	}

	size_t GeometryEncoder::put(const uint8_t* data, size_t count)
	{
		size_t pos = buffer.size();
		buffer.resize(buffer.size() + count);
		memcpy(buffer.data() + pos, data, count);
		if(count>= settings->geometryBufferCutoffSize)
		{
			TELEPORT_CERR<<"Data too big for geometry buffer cutoff size."<<std::endl;
		}
		return pos;
	}

	bool GeometryEncoder::attemptQueueData()
	{
		//If queueing the data will cause the queuedBuffer to exceed the cutoff size.
		if (buffer.size() + queuedBuffer.size() > settings->geometryBufferCutoffSize)
		{
			//Never leave queuedBuffer empty, if there is something to queue up (even if it is too large).
			if (queuedBuffer.size() == 0)
			{
				size_t position = queuedBuffer.size();
				queuedBuffer.resize(queuedBuffer.size() + buffer.size());

				memcpy(queuedBuffer.data() + position, buffer.data(), buffer.size());
				buffer.clear();
			}

			return false;
		}
		else
		{
			size_t position = queuedBuffer.size();
			queuedBuffer.resize(queuedBuffer.size() + buffer.size());

			memcpy(queuedBuffer.data() + position, buffer.data(), buffer.size());
			buffer.clear();

			return true;
		}
	}
}