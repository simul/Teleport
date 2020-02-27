#pragma once

#include <map>
#include <unordered_map>

#include "libavstream/geometry/mesh_interface.hpp"
#include "SimulCasterServer/GeometryStore.h"

#include "Components/LightComponent.h"

/*! The Geometry Source keeps all the geometry ready for streaming, and returns geometry
	data in glTF-style when asked for.

	It handles geometry for multiple clients, so each client will only want a subset.
*/
class GeometrySource
{
public:
	GeometrySource();
	~GeometrySource();
	void Initialise(class ARemotePlayMonitor* monitor, UWorld* world);
	void ClearData();

	avs::uid AddMesh(class UMeshComponent* MeshComponent);
	
	//Adds the node to the geometry source; decomposing the node to its base components. Will update the node if it has already been processed before.
	//	component : Scene component the node represents.
	//	forceUpdate : Causes node data to be extracted, even if it has been before.
	//Return UID of node.
	avs::uid AddNode(USceneComponent* component, bool forceUpdate = false);
	
	//Adds the material to the geometry source, where it is processed into a streamable material.
	//Returns the UID of the processed material information, or 0 if a nullptr is passed.
	avs::uid AddMaterial(class UMaterialInterface *materialInterface);

	avs::uid AddShadowMap(const FStaticShadowDepthMapData* shadowDepthMapData);

	//Compresses any textures that were found during decomposition of actors.
	//Split-off so all texture compression can happen at once with a progress bar.
	void CompressTextures();

	inline SCServer::GeometryStore& GetStorage()
	{
		return storage;
	}
protected:
	struct Mesh;
	struct MaterialChangedInfo;

	class ARemotePlayMonitor* Monitor;

	SCServer::GeometryStore storage;

	std::map<avs::uid, std::vector<avs::vec3>> scaledPositionBuffers;
	std::map<avs::uid, std::vector<avs::Vec4<signed char>>> tangentNormalBuffers; //Stores data to the corrected tangent and normal buffers.
	std::map<avs::uid, std::vector<FVector2D>> processedUVs;

	std::map<FName, avs::uid> processedNodes; //Nodes we have already stored in the GeometrySource; <Level Unique Node Name, Node Identifier>.
	std::unordered_map<UStaticMesh*, Mesh> processedMeshes; //Meshes we have already stored in the GeometrySource; the pointer points to the uid of the stored mesh information.
	std::unordered_map<UMaterialInterface*, MaterialChangedInfo> processedMaterials; //Materials we have already stored in the GeometrySource; the pointer points to the uid of the stored material information.
	std::unordered_map<UTexture*, avs::uid> processedTextures; //Textures we have already stored in the GeometrySource; the pointer points to the uid of the stored texture information.
	std::unordered_map<const FStaticShadowDepthMapData*, avs::uid> processedShadowMaps;

	void PrepareMesh(Mesh* mesh);
	bool ExtractMesh(Mesh* mesh, uint8 lodIndex);
	void ExtractMeshData(Mesh* mesh, FStaticMeshLODResources& lod, avs::AxesStandard extractToBasis);

	//Add a node that represents a mesh.
	//	meshComponent : Mesh the node will represent.
	//	oldID : ID being used by this node, if zero it will create a new ID.
	//Returns the ID of the node added.
	avs::uid AddMeshNode(UMeshComponent* meshComponent, avs::uid oldID);
	//Add a node that represents a light.
	//	lightComponent : Light the node will represent.
	//	oldID : ID being used by this node, if zero it will create a new ID.
	//Returns the ID of the node added.
	avs::uid AddShadowMapNode(ULightComponent* lightComponent, avs::uid oldID);

	//Returns component transform.
	//	component : Component we want the transform of.
	avs::Transform GetComponentTransform(USceneComponent* component);

	//Determines if the texture has already been stored, and pulls apart the texture data and stores it in a avs::Texture.
	//	texture : UTexture to pull the texture data from.
	//Returns the uid for this texture.
	avs::uid AddTexture(UTexture *texture);

	//Returns the first texture in the material chain.
	//	materialInterface : The interface of the material we are decomposing.
	//	propertyChain : The material property we are decomposing.
	//	outTexture : Texture related to the chain to output into.
	void GetDefaultTexture(UMaterialInterface *materialInterface, EMaterialProperty propertyChain, avs::TextureAccessor &outTexture);

	//Decomposes the material into the texture, and outFactor supplied.
	//	materialInterface : The interface of the material we are decomposing.
	//	propertyChain : The material property we are decomposing.
	//	outTexture : Texture related to the chain to output into.
	//	outFactor : Factor related to the chain to output into.
	void DecomposeMaterialProperty(UMaterialInterface *materialInterface, EMaterialProperty propertyChain, avs::TextureAccessor &outTexture, float &outFactor);
	void DecomposeMaterialProperty(UMaterialInterface *materialInterface, EMaterialProperty propertyChain, avs::TextureAccessor &outTexture, avs::vec3 &outFactor);
	void DecomposeMaterialProperty(UMaterialInterface *materialInterface, EMaterialProperty propertyChain, avs::TextureAccessor &outTexture, avs::vec4 &outFactor);

	//Decomposes UMaterialExpressionTextureSample; extracting the texture, and tiling data.
	//	materialInterface : The base class of the material we are decomposing.
	//	textureSample : The expression we want to extract/decompose the data from.
	//	outTexture : Texture related to the chain to output into.
	//Returns the amount of expressions that were handled in the chain.
	size_t DecomposeTextureSampleExpression(UMaterialInterface* materialInterface, class UMaterialExpressionTextureSample* textureSample, avs::TextureAccessor& outTexture);
};

struct ShadowMapData
{
	const FStaticShadowDepthMap& depthTexture;
	const FVector4& position;
	const FQuat& orientation;

	ShadowMapData(const FStaticShadowDepthMap& _depthTexture, const FVector4& _position, const FQuat& _orientation)
		:depthTexture(_depthTexture), position(_position), orientation(_orientation) {}

	ShadowMapData(const ULightComponent* light)
		:depthTexture(light->StaticShadowDepthMap),
		position(light->GetLightPosition()),
		orientation(light->GetDirection().Rotation().Quaternion())
		{}
};