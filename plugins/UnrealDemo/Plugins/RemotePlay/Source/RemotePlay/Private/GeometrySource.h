#pragma once
#include <libavstream/geometry/mesh_interface.hpp>
#include <map>
#include <unordered_map>
#include <CoreMinimal.h>

/*! The Geometry Source keeps all the geometry ready for streaming, and returns geometry
	data in glTF-style when asked for.

	It handles geometry for multiple clients, so each client will only want a subset.
*/
class GeometrySource : public avs::GeometrySourceBackendInterface
{
public:
	GeometrySource();
	~GeometrySource();
	void clearData();

	avs::uid AddMesh(class UMeshComponent *MeshComponent);
	avs::uid AddStreamableMeshComponent(UMeshComponent *MeshComponent);
	avs::uid CreateNode(const struct FTransform& transform, avs::uid data_uid, avs::NodeDataType data_type);

	void Tick();

	// Inherited via GeometrySourceBackendInterface
	virtual size_t getNodeCount() const override;
	virtual avs::uid getNodeUid(size_t index) const override;
	virtual std::shared_ptr<avs::DataNode> getNode(avs::uid node_uid) const override;
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


protected:
	struct Mesh;

	struct GeometryInstance
	{
		class UStreamableGeometryComponent* Geometry;
		//unsigned long long SentFrame;
	};
	mutable TMap<avs::uid, TSharedPtr<Mesh>> Meshes;
	// We store buffers, views and accessors in one big list. But we should
	// PROBABLY refcount these so that unused ones can be cleared.
	mutable std::map<avs::uid, avs::Accessor> accessors;
	mutable std::map<avs::uid, avs::BufferView> bufferViews;
	mutable std::map<avs::uid, avs::GeometryBuffer> geometryBuffers;
	mutable std::map<avs::uid, std::shared_ptr<avs::DataNode>> nodes;

	std::unordered_map<UTexture*, avs::uid> processedTextures; //Textures we have already stored in the GeometrySource; the pointer points to the uid of the stored texture information.
	std::vector<UMaterialInterface*> processedMaterials; //Materials we have already stored in the GeometrySource.

	std::map<avs::uid, avs::Texture> textures;
	std::map<avs::uid, avs::Material> materials;

	void PrepareMesh(Mesh &m);
	void SendMesh(Mesh &m);
	bool InitMesh(Mesh *mesh, uint8 lodIndex) const;

	//Determines if the texture has already been stored, and pulls apart the texture data and stores it in a avs::Texture.
	//	texture : UTexture to pull the texture data from.
	//Returns the uid for this texture.
	avs::uid StoreTexture(UTexture *texture);
};
