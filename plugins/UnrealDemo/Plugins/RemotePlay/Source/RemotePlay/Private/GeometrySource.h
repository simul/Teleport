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
	void Initialize(ARemotePlayMonitor* monitor, UWorld* world);
	void clearData();

	avs::uid AddMesh(class UStaticMesh *);
	avs::uid AddStreamableMeshComponent(UMeshComponent *MeshComponent);
	
	avs::uid AddNode(avs::uid parent_uid, USceneComponent* component, bool forceTransformUpdate = false);
	avs::uid CreateNode(USceneComponent* component, avs::uid data_uid, avs::NodeDataType data_type,const std::vector<avs::uid> &mat_uids);

	avs::uid GetRootNodeUid();
	bool GetRootNode(std::shared_ptr<avs::DataNode>& node);

	const std::vector<avs::uid>& GetHandActorUIDs()
	{
		return handUIDs;
	};
	
	//Adds the material to the geometry source, where it is processed into a streamable material.
	//Returns the UID of the processed material information, or 0 if a nullptr is passed.
	avs::uid AddMaterial(class UMaterialInterface *materialInterface);

	avs::uid AddShadowMap(const FStaticShadowDepthMapData* shadowDepthMapData);
	void Tick();

	// Inherited via GeometrySourceBackendInterface
	virtual std::vector<avs::uid> getNodeUIDs() const override;
	virtual bool getNode(avs::uid node_uid, std::shared_ptr<avs::DataNode> & outNode) const override;
	virtual std::map<avs::uid, std::shared_ptr<avs::DataNode>>& getNodes() const override;

	virtual size_t getMeshCount() const override;
	virtual avs::uid getMeshUid(size_t index) const override;
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

	basisu::basis_compressor_params basisCompressorParams; //Parameters for basis compressor.

	mutable TMap<avs::uid, TSharedPtr<Mesh>> Meshes;
	// We store buffers, views and accessors in one big list. But we should
	// PROBABLY refcount these so that unused ones can be cleared.
	mutable std::map<avs::uid, avs::Accessor> accessors;
	mutable std::map<avs::uid, avs::BufferView> bufferViews;
	mutable std::map<avs::uid, avs::GeometryBuffer> geometryBuffers;
	mutable std::map<avs::uid, std::shared_ptr<avs::DataNode>> nodes;

	std::unordered_map<UTexture*, avs::uid> decomposedTextures; //Textures we have already stored in the GeometrySource; the pointer points to the uid of the stored texture information.
	std::unordered_map<UMaterialInterface*, avs::uid> decomposedMaterials; //Materials we have already stored in the GeometrySource; the pointer points to the uid of the stored material information.
	std::map<FName, avs::uid> decomposedNodes; //Nodes we have already stored in the GeometrySource; <Level Unique Node Name, Node Identifier>.
	std::unordered_map<const FStaticShadowDepthMapData*, avs::uid> storedShadowMaps;

	std::map<avs::uid, avs::Texture> textures;
	std::map<avs::uid, avs::Material> materials;
	std::map<avs::uid, avs::Texture> shadowMaps;

	std::vector<avs::LightNodeResources> lightNodes; //List of all light nodes; prevents having to search for them every geometry tick.
	
	mutable std::map<avs::uid, std::vector<avs::vec3>> scaledPositionBuffers;
	mutable std::map<avs::uid, std::vector<FVector2D>> processedUVs;
	avs::uid rootNodeUid;

	void PrepareMesh(Mesh &m);
	void SendMesh(Mesh &m);
	bool InitMesh(Mesh *mesh, uint8 lodIndex) const;

	//Updates the transform of the node.
	//	node : Node which is to be updated.
	//	component : Component the node corresponds to.
	void UpdateNodeTransform(std::shared_ptr<avs::DataNode>& node, USceneComponent* component);

	//Determines if the texture has already been stored, and pulls apart the texture data and stores it in a avs::Texture.
	//	texture : UTexture to pull the texture data from.
	//Returns the uid for this texture.
	avs::uid StoreTexture(UTexture *texture);

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

	class ARemotePlayMonitor* Monitor;

	std::vector<avs::uid> handUIDs;
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