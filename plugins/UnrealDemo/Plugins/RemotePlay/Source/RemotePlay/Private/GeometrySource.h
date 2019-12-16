#pragma once
#include <libavstream/geometry/mesh_interface.hpp>
#include <map>
#include <unordered_map>
#include <CoreMinimal.h>
#include <Runtime/Engine/Classes/Components/LightComponent.h>
#include <Runtime/Engine/Classes/Engine/MapBuildDataRegistry.h>

#include "basisu_comp.h"

/*! The Geometry Source keeps all the geometry ready for streaming, and returns geometry
	data in glTF-style when asked for.

	It handles geometry for multiple clients, so each client will only want a subset.
*/
class GeometrySource : public avs::GeometrySourceBackendInterface
{
public:
	GeometrySource();
	~GeometrySource();
	void Initialise(class ARemotePlayMonitor* monitor, UWorld* world);
	void ClearData();

	avs::uid AddMesh(class UMeshComponent* MeshComponent);
	
	//Adds the node to the geometry source; decomposing the node to its base components. Will update the node if it has already been processed before.
	//	component : Scene component the node represents.
	//Return UID of node.
	avs::uid AddNode(USceneComponent* component);
	//Returns UID of node that represents the passed component; will add the node if it has not been processed before.
	//	component : Scene component the node represents.
	avs::uid GetNode(USceneComponent* component);

	const std::vector<avs::uid>& GetHandActorUIDs()
	{
		return handIDs;
	};
	
	//Adds the material to the geometry source, where it is processed into a streamable material.
	//Returns the UID of the processed material information, or 0 if a nullptr is passed.
	avs::uid AddMaterial(class UMaterialInterface *materialInterface);

	avs::uid AddShadowMap(const FStaticShadowDepthMapData* shadowDepthMapData);
	void Tick();

	//Compresses any textures that were found during decomposition of actors.
	//Split-off so all texture compression can happen at once with a progress bar.
	void CompressTextures();

	// Inherited via GeometrySourceBackendInterface
	virtual std::vector<avs::uid> getNodeUIDs() const override;
	virtual bool getNode(avs::uid node_uid, avs::DataNode& outNode) const override;
	virtual const std::map<avs::uid, avs::DataNode>& getNodes() const override;

	virtual size_t getMeshPrimitiveArrayCount(avs::uid mesh_uid) const override;
	virtual bool getMeshPrimitiveArray(avs::uid mesh_uid, size_t array_index, avs::PrimitiveArray & primitiveArray) const override;
	virtual bool getAccessor(avs::uid accessor_uid, avs::Accessor & accessor) const override;
	virtual bool getBufferView(avs::uid buffer_view_uid, avs::BufferView & bufferView) const override;
	virtual bool getBuffer(avs::uid buffer_uid, avs::GeometryBuffer & buffer) const override;

	virtual std::vector<avs::uid> getTextureUIDs() const override;
	virtual bool getTexture(avs::uid texture_uid, avs::Texture & outTexture) const override;

	virtual std::vector<avs::uid> getMaterialUIDs() const override;
	virtual bool getMaterial(avs::uid material_uid, avs::Material & outMaterial) const override;

	virtual std::vector<avs::uid> getShadowMapUIDs() const override;
	virtual bool getShadowMap(avs::uid shadow_uid, avs::Texture& outShadowMap) const override;
	virtual const std::vector<avs::LightNodeResources>& getLightNodes() const;
protected:
	struct Mesh;

	//Stores data on a texture that is to be compressed.
	struct PrecompressedTexture
	{
		FString basisFilePath;
		FTextureSource& textureSource;
	};

	class ARemotePlayMonitor* Monitor;

	basisu::basis_compressor_params basisCompressorParams; //Parameters for basis compressor.

	// We store buffers, views and accessors in one big list. But we should
	// PROBABLY refcount these so that unused ones can be cleared.
	std::map<avs::uid, avs::Accessor> accessors;
	std::map<avs::uid, avs::BufferView> bufferViews;
	std::map<avs::uid, avs::GeometryBuffer> geometryBuffers;
	std::map<avs::uid, std::vector<avs::vec3>> scaledPositionBuffers;
	std::map<avs::uid, std::vector<FVector2D>> processedUVs;

	std::unordered_map<UStaticMesh*, avs::uid> processedMeshes; //Meshes we have already stored in the GeometrySource; the pointer points to the uid of the stored mesh information.
	std::unordered_map<UTexture*, avs::uid> processedTextures; //Textures we have already stored in the GeometrySource; the pointer points to the uid of the stored texture information.
	std::unordered_map<UMaterialInterface*, avs::uid> processedMaterials; //Materials we have already stored in the GeometrySource; the pointer points to the uid of the stored material information.
	std::map<FName, avs::uid> processedNodes; //Nodes we have already stored in the GeometrySource; <Level Unique Node Name, Node Identifier>.
	std::unordered_map<const FStaticShadowDepthMapData*, avs::uid> processedShadowMaps;

	std::map<avs::uid, avs::DataNode> nodes;
	std::map<avs::uid, Mesh> meshes;
	std::map<avs::uid, avs::Texture> textures;
	std::map<avs::uid, avs::Material> materials;
	std::map<avs::uid, avs::Texture> shadowMaps;

	std::map<avs::uid, PrecompressedTexture> texturesToCompress; //Map of textures that need compressing. <ID of the texture; file path to store the basis file>

	std::vector<avs::LightNodeResources> lightNodes; //List of all light nodes; prevents having to search for them every geometry tick.

	std::vector<avs::uid> handIDs; //IDs of the nodes that represent the hands that are in use.

	//Returns ID of the node that represents the component; to prevent double search in GetNode when a node doesn't exist.
	//	component : Scene component the node represents.
	//	nodeIterator : Iterator returned when searching for the node in processedNodes.
	avs::uid AddNode_Internal(USceneComponent* component, std::map<FName, avs::uid>::iterator nodeIterator);
	//Returns the iterator returned when searching for the passed component.
	//	component : Scene component the node represents.
	std::map<FName, avs::uid>::iterator FindNodeIterator(USceneComponent* component);

	void PrepareMesh(Mesh* mesh);
	bool InitMesh(Mesh* mesh, uint8 lodIndex);

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
	size_t DecomposeTextureSampleExpression(UMaterialInterface* materialInterface, UMaterialExpressionTextureSample* textureSample, avs::TextureAccessor& outTexture);
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