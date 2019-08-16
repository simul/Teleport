#include "GeometrySource.h"
#include "StreamableGeometryComponent.h"
#include "Engine/StaticMesh.h"
#include "Rendering/StaticMeshVertexBuffer.h"
#include "Rendering/PositionVertexBuffer.h"
#include "StaticMeshResources.h"
#include <map>
#if 0
#include <random>
std::default_random_engine generator;
std::uniform_int_distribution<int> distribution(1, 6);
int dice_roll = distribution(generator);
#endif

struct GeometrySource::Mesh
{
	~Mesh()
	{
	}
	class UStaticMesh* StaticMesh;
	unsigned long long SentFrame;
	bool Confirmed;
	std::vector<avs::PrimitiveArray> primitiveArrays;
	std::vector<avs::Attribute> attributes;
};

avs::AttributeSemantic IndexToSemantic(int index)
{
	switch (index)
	{
	case 0:
		return avs::AttributeSemantic::POSITION;
	case 1:
		return avs::AttributeSemantic::NORMAL;
	case 2:
		return avs::AttributeSemantic::TANGENT;
	case 3:
		return avs::AttributeSemantic::TEXCOORD_0;
	case 4:
		return avs::AttributeSemantic::TEXCOORD_1;
	case 5:
		return avs::AttributeSemantic::COLOR_0;
	case 6:
		return avs::AttributeSemantic::JOINTS_0;
	case 7:
		return avs::AttributeSemantic::WEIGHTS_0;
	};
	return avs::AttributeSemantic::TEXCOORD_0;
}

bool GeometrySource::InitMesh(GeometrySource::Mesh *m, FStaticMeshLODResources &lod) const
{
	m->primitiveArrays.resize(lod.Sections.Num());
	for (size_t i = 0; i < m->primitiveArrays.size(); i++)
	{
		auto &section = lod.Sections[i];
		FPositionVertexBuffer &pb = lod.VertexBuffers.PositionVertexBuffer;
		FStaticMeshVertexBuffer &vb = lod.VertexBuffers.StaticMeshVertexBuffer;
		auto &pa = m->primitiveArrays[i];
		// 1 position. 0 or 1 tangent/normal. + num texcoords.
		pa.attributeCount = 1 + (vb.GetTangentData() ? 1 : 0) + (vb.GetTexCoordData() ? vb.GetNumTexCoords() : 0);
		pa.attributes = new avs::Attribute[pa.attributeCount];
		auto AddBufferAndView = [this](GeometrySource::Mesh *m,avs::uid &b_uid,size_t num,size_t stride,const void *data)
		{
			avs::BufferView &bv = bufferViews[b_uid];
			bv.byteOffset = 0;
			bv.byteLength = num * stride;
			bv.byteStride = stride;
			bv.buffer = avs::GenerateUid();
			avs::GeometryBuffer& b = geometryBuffers[bv.buffer];
			b.byteLength = bv.byteLength;
			b.data = (const uint8_t *)data;			// Remember, just a pointer: we don't own this data.
		};
		size_t idx = 0;
		// Position:
		{
			avs::Attribute &attr = pa.attributes[idx++];
			attr.accessor = avs::GenerateUid();
			attr.semantic = avs::AttributeSemantic::POSITION;
			avs::Accessor &a = accessors[attr.accessor];
			a.byteOffset = 0;
			a.type = avs::Accessor::DataType::VEC3;
			a.componentType = avs::Accessor::ComponentType::FLOAT;
			a.count = pb.GetNumVertices();
			a.bufferView = avs::GenerateUid();
			AddBufferAndView(m,a.bufferView, pb.GetNumVertices(), pb.GetStride(), pb.GetVertexData());
		}
		// Normal and tangent are packed together as XZ:
		if(vb.GetTangentData())
		{
			avs::Attribute &attr = pa.attributes[idx++];
			attr.accessor = avs::GenerateUid();
			attr.semantic = avs::AttributeSemantic::TANGENTNORMALXZ;
			avs::Accessor &a = accessors[attr.accessor];
			a.byteOffset = 0;
			a.type = avs::Accessor::DataType::VEC2;
			a.componentType = avs::Accessor::ComponentType::UINT;		// VEC2 of UINT's unpack to two VEC4's of signed BYTE.
			if (vb.GetTangentSize() == 16)
				a.type = avs::Accessor::DataType::VEC4;					// VEC4 of UINT's unpack to two VEC4's of signed SHORT
			size_t tangentStride = vb.GetTangentSize() / vb.GetNumVertices();
			a.count = vb.GetNumVertices();// same as pb???
			a.bufferView = avs::GenerateUid();
			AddBufferAndView(m, a.bufferView, vb.GetNumVertices(), tangentStride, vb.GetTangentData());
		}
		for (size_t j = 0; j < vb.GetNumTexCoords(); j++)
		{
			avs::Attribute &attr = pa.attributes[idx++];
			attr.accessor = avs::GenerateUid();
			attr.semantic = j==0?avs::AttributeSemantic::TEXCOORD_0: avs::AttributeSemantic::TEXCOORD_1;
			avs::Accessor &a = accessors[attr.accessor];
			a.byteOffset = 0;
			a.type = avs::Accessor::DataType::VEC4;
			a.componentType = avs::Accessor::ComponentType::FLOAT;
			a.count = vb.GetNumVertices();// same as pb???
			a.bufferView = avs::GenerateUid();
			AddBufferAndView(m, a.bufferView, vb.GetNumVertices(), vb.GetTexCoordSize()/  vb.GetNumTexCoords()/ vb.GetNumVertices(), vb.GetTexCoordData());
		}
		pa.indices_accessor = avs::GenerateUid();

		FRawStaticIndexBuffer &ib = lod.IndexBuffer;
		avs::Accessor &i_a = accessors[pa.indices_accessor];
		i_a.byteOffset = 0;
		i_a.type = avs::Accessor::DataType::SCALAR;
		i_a.componentType = ib.Is32Bit()?avs::Accessor::ComponentType::UINT: avs::Accessor::ComponentType::USHORT;
		i_a.count = ib.GetNumIndices();// same as pb???
		i_a.bufferView = avs::GenerateUid();
		FIndexArrayView arr = ib.GetArrayView();
		AddBufferAndView(m, i_a.bufferView, ib.GetNumIndices(), ib.Is32Bit()?4:2, (const void*)((uint64*)&arr)[0]);

		pa.material = avs::GenerateUid();
		pa.primitiveMode = avs::PrimitiveMode::TRIANGLES;
	}
	return true;
}

GeometrySource::GeometrySource()
{
}

GeometrySource::~GeometrySource()
{
	Meshes.Empty();
	GeometryInstances.Empty();

	//Free all allocated data for the textures.
	for(auto &texMap : textures)
	{
		delete[] texMap.second.data;
	}
}

// By adding a m, we also add a pipe, including the InputMesh, which must be configured with the appropriate 
avs::uid GeometrySource::AddMesh(UStaticMesh *StaticMesh)
{
	avs::uid uid = avs::GenerateUid();
	TSharedPtr<Mesh> m ( new Mesh);
	Meshes.Add(uid, m);
	m->StaticMesh = StaticMesh;
	m->SentFrame = (unsigned long long)0;
	m->Confirmed = false;
	PrepareMesh(*m);
	return uid;
}
 
avs::uid GeometrySource::AddStreamableActor(UStreamableGeometryComponent *StreamableGeometryComponent)
{
	avs::uid mesh_uid;
	UStaticMesh *StaticMesh = StreamableGeometryComponent->GetMesh()->GetStaticMesh();
	bool already_got_mesh = false;
	for (auto &i : Meshes)
	{
		if (i.Value->StaticMesh == StaticMesh)
		{
			already_got_mesh = true;
			mesh_uid = i.Key;
		}
	}
	if (!already_got_mesh)
	{
		mesh_uid =AddMesh(StaticMesh);
	}
	TSharedPtr<GeometryInstance> geom(new GeometryInstance);
	geom->Geometry= StreamableGeometryComponent;
	bool already_got_geom = false;
	avs::uid node_uid=0;
	for (auto i : GeometryInstances)
	{
		if (i.Value->Geometry == geom->Geometry)
		{
			already_got_geom = true;
			node_uid = i.Key;
		}
	}
	if (!already_got_geom)
	{
		node_uid = avs::GenerateUid();
		GeometryInstances.Add(node_uid, geom);
	}
#ifdef DISABLE
#endif
	//USceneComponent* sc = (USceneComponent*)StreamableGeometryComponent;
	//FTransform modelTranform = sc->GetRelativeTransform();

	///ASSUMPTION: Assuming that if a material has been processed, then its sub-resources have been processed.

	//Assuming there is only one material.
	UMaterialInterface *materialInterface = StreamableGeometryComponent->GetMaterial(0);

	//Store the material if it exists, and we have not already processed it.
	if(materialInterface && std::find(processedMaterials.begin(), processedMaterials.end(), materialInterface) == processedMaterials.end())
	{
		const unsigned long long DUMMY_TEX_COORD = 0;

		UTexture* diffuseTex = StreamableGeometryComponent->GetTexture(EMaterialProperty::MP_BaseColor);
		//Assuming if it is used on one it is used on them all.
		UTexture* metalRoughOcclusTex = StreamableGeometryComponent->GetTexture(EMaterialProperty::MP_Metallic);
		UTexture* normalTex = StreamableGeometryComponent->GetTexture(EMaterialProperty::MP_Normal);
		UTexture* emissiveTex = StreamableGeometryComponent->GetTexture(EMaterialProperty::MP_EmissiveColor);

		avs::Material newMaterial;

		newMaterial.name = TCHAR_TO_ANSI(*materialInterface->GetName());

		//Store the texture if it exists.
		if(diffuseTex)
		{
			newMaterial.pbrMetallicRoughness.baseColorTexture = avs::TextureAccessor{StoreTexture(diffuseTex), DUMMY_TEX_COORD};
		}

		if(metalRoughOcclusTex)
		{
			avs::TextureAccessor metNorOccAccessor = {StoreTexture(metalRoughOcclusTex), DUMMY_TEX_COORD};

			newMaterial.pbrMetallicRoughness.metallicRoughnessTexture = metNorOccAccessor;
			newMaterial.normalTexture = metNorOccAccessor;
			newMaterial.occlusionTexture = metNorOccAccessor;
		}

		if(normalTex)
		{
			newMaterial.normalTexture = avs::TextureAccessor{StoreTexture(normalTex), DUMMY_TEX_COORD};
		}

		if(emissiveTex)
		{
			newMaterial.emissiveTexture = avs::TextureAccessor{StoreTexture(emissiveTex), DUMMY_TEX_COORD};
		}
				
		avs::uid mat_uid = avs::GenerateUid();
		materials[mat_uid] = newMaterial;

		processedMaterials.push_back(materialInterface);
	}

	return node_uid;
}

void GeometrySource::Tick()
{
	for (auto a : ToAdd)
	{
		AddStreamableActor(a);
	}
	ToAdd.Empty();
}

void GeometrySource::PrepareMesh(Mesh &m)
{
	// We will pre-encode the mesh to prepare it for streaming.
	UStaticMesh* StaticMesh = m.StaticMesh;
	int verts = StaticMesh->GetNumVertices(0);
	FStaticMeshRenderData *StaticMeshRenderData = StaticMesh->RenderData.Get();
	if (!StaticMeshRenderData->IsInitialized())
	{
		UE_LOG(LogRemotePlay, Warning, TEXT("StaticMeshRenderData Not ready"));
		return;
	}
	FStaticMeshLODResources &LODResources = StaticMeshRenderData->LODResources[0];

	FPositionVertexBuffer &PositionVertexBuffer		= LODResources.VertexBuffers.PositionVertexBuffer;
	FStaticMeshVertexBuffer &StaticMeshVertexBuffer = LODResources.VertexBuffers.StaticMeshVertexBuffer;

	uint32 pos_stride		= PositionVertexBuffer.GetStride();
	const float *pos_data	= (const float*)PositionVertexBuffer.GetVertexData();

	int numVertices = PositionVertexBuffer.GetNumVertices();
	for (int i = 0; i < numVertices; i++)
	{
		pos_data += pos_stride / sizeof(float);
	}
	//LODResources.IndexBuffer.GetCopy(m.Indices);
#ifdef DISABLE
#endif
}

avs::uid GeometrySource::StoreTexture(UTexture * texture)
{
	avs::uid texture_uid;
	auto it = processedTextures.find(texture);

	//Retrieve the uid if we have already processed this texture.
	if(it != processedTextures.end())
	{
		texture_uid = it->second;
	}
	//Otherwise, store it.
	else
	{
		texture_uid = avs::GenerateUid();

		std::string textureName = TCHAR_TO_ANSI(*texture->GetName());

		//Assuming the first running platform is the desired running platform.
		FTexture2DMipMap baseMip = texture->GetRunningPlatformData()[0]->Mips[0];
		FTextureSource &textureSource = texture->Source;
		ETextureSourceFormat unrealFormat = textureSource.GetFormat();

		uint32_t width = baseMip.SizeX;
		uint32_t height = baseMip.SizeY;
		uint32_t depth = baseMip.SizeZ; ///!!! Is this actually where Unreal stores its depth information for a texture? !!!
		uint32_t bytesPerPixel = textureSource.GetBytesPerPixel();
		uint32_t arrayCount = textureSource.GetNumSlices(); ///!!! Is this actually the array count? !!!
		uint32_t mipCount = textureSource.GetNumMips();
		avs::TextureFormat format;

		switch(unrealFormat)
		{
			case ETextureSourceFormat::TSF_Invalid:
				format = avs::TextureFormat::INVALID;
				break;
			case ETextureSourceFormat::TSF_G8:
				format = avs::TextureFormat::G8;
				break;
			case ETextureSourceFormat::TSF_BGRA8:
				format = avs::TextureFormat::BGRA8;
				break;
			case ETextureSourceFormat::TSF_BGRE8:
				format = avs::TextureFormat::BGRE8;
				break;
			case ETextureSourceFormat::TSF_RGBA16:
				format = avs::TextureFormat::RGBA16;
				break;
			case ETextureSourceFormat::TSF_RGBA16F:
				format = avs::TextureFormat::RGBA16F;
				break;
			case ETextureSourceFormat::TSF_RGBA8:
				format = avs::TextureFormat::RGBA8;
				break;
			case ETextureSourceFormat::TSF_RGBE8:
				format = avs::TextureFormat::INVALID;
				break;
			case ETextureSourceFormat::TSF_MAX:
				format = avs::TextureFormat::INVALID;
				break;
		}

		TArray<uint8> mipData;
		textureSource.GetMipData(mipData, 0);		

		//Channels * Width * Height
		std::size_t texSize = 4 * baseMip.SizeX * baseMip.SizeY;
		unsigned char* rawPixelData = new unsigned char[texSize];
		memcpy(rawPixelData, mipData.GetData(), texSize);		

		//We're using a single sampler for now.
		avs::uid sampler_uid = 0;

		textures[texture_uid] = {textureName, width, height, depth, bytesPerPixel, arrayCount, mipCount, format, rawPixelData, sampler_uid};
		processedTextures[texture] = texture_uid;
	}

	return texture_uid;
}


size_t GeometrySource::getNodeCount() const
{
	return size_t();
}

avs::uid GeometrySource::getNodeUid(size_t index) const
{
	return avs::uid();
}

std::shared_ptr<avs::DataNode> GeometrySource::getNode(avs::uid node_uid) const
{
	return nodes[node_uid];
}
std::map<avs::uid, std::shared_ptr<avs::DataNode>>& GeometrySource::getNodes() const
{
	return nodes;
}

size_t GeometrySource::getMeshCount() const
{
	return Meshes.Num();
}

avs::uid GeometrySource::getMeshUid(size_t index) const
{
	TArray<avs::uid> MeshUids;
	Meshes.GenerateKeyArray(MeshUids);
	return MeshUids[index];
}

size_t GeometrySource::getMeshPrimitiveArrayCount(avs::uid mesh_uid) const
{
	auto &mesh = Meshes[mesh_uid];
	UStaticMesh *staticMesh = mesh->StaticMesh;
	if (!staticMesh->RenderData)
		return 0;
	auto &lods=staticMesh->RenderData->LODResources;
	if (!lods.Num())
		return 0;
	return lods[0].Sections.Num();
}

bool GeometrySource::getMeshPrimitiveArray(avs::uid mesh_uid, size_t array_index, avs::PrimitiveArray & primitiveArray) const
{
	GeometrySource::Mesh *mesh = Meshes[mesh_uid].Get();
	UStaticMesh *staticMesh = mesh->StaticMesh;
	auto &lods = staticMesh->RenderData->LODResources;
	if (!lods.Num())
		return false;
	auto &lod=lods[0];
	if (!mesh->primitiveArrays.size())
	{
		InitMesh(mesh, lod);
	}
	primitiveArray = mesh->primitiveArrays[array_index];

	return false;
}

bool GeometrySource::getAccessor(avs::uid accessor_uid, avs::Accessor & accessor) const
{
	auto it = accessors.find(accessor_uid);
	if (it == accessors.end())
	{
		UE_LOG(LogRemotePlay, Error, TEXT("Accessor not found!"));
		return false;
	}
	accessor = accessors[accessor_uid];
	return true;
}

bool GeometrySource::getBufferView(avs::uid buffer_view_uid, avs::BufferView & bufferView) const
{
	auto it = bufferViews.find(buffer_view_uid);
	if (it == bufferViews.end())
	{
		UE_LOG(LogRemotePlay, Error, TEXT("Buffer View not found!"));
		return false;
	}
	bufferView = bufferViews[buffer_view_uid];
	return true;
}

bool GeometrySource::getBuffer(avs::uid buffer_uid, avs::GeometryBuffer & buffer) const
{
	auto it = geometryBuffers.find(buffer_uid);
	if (it == geometryBuffers.end())
	{
		UE_LOG(LogRemotePlay, Error, TEXT("Buffer not found!"));
		return false;
	}
	buffer = geometryBuffers[buffer_uid];
	return true;
}

std::vector<avs::uid> GeometrySource::getTextureUIDs() const
{
	std::vector<avs::uid> textureUIDs(textures.size());

	size_t i = 0;
	for(const auto &it : textures)
	{
		textureUIDs[i++] = it.first;
	}

	return textureUIDs;
}

bool GeometrySource::getTexture(avs::uid texture_uid, avs::Texture & outTexture) const
{
	//Assuming an incorrect texture uid should not happen, or at least not frequently.
	try
	{
		outTexture = textures.at(texture_uid);

		return true;
	}
	catch(std::out_of_range oor)
	{
		return false;
	}
}

std::vector<avs::uid> GeometrySource::getMaterialUIDs() const
{
	std::vector<avs::uid> materialUIDs(materials.size());

	size_t i = 0;
	for(const auto &it : materials)
	{
		materialUIDs[i++] = it.first;
	}

	return materialUIDs;
}

bool GeometrySource::getMaterial(avs::uid material_uid, avs::Material & outMaterial) const
{
	//Assuming an incorrect material uid should not happen, or at least not frequently.
	try
	{
		outMaterial = materials.at(material_uid);

		return true;
	}
	catch(std::out_of_range oor)
	{
		return false;
	}
}
