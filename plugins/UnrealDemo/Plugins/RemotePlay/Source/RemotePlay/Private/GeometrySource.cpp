#include "GeometrySource.h"
#include "StreamableGeometryComponent.h"
#include "Engine/StaticMesh.h"
#include "Rendering/StaticMeshVertexBuffer.h"
#include "Rendering/PositionVertexBuffer.h"
#include "StaticMeshFResources.h"
#include <map>
#if 0
#include <random>
std::default_random_engine generator;
std::uniform_int_distribution<int> distribution(1, 6);
int dice_roll = distribution(generator);
#endif
struct GeometrySource::Mesh
{
	~Mesh();
	class UStaticMesh* StaticMesh;
	unsigned long long SentFrame;
	bool Confirmed;
	std::vector<avs::PrimitiveArray> primitiveArrays;
	std::vector<avs::Attribute> attributes;
	std::map<avs::uid,avs::Accessor> accessors;
	std::map<avs::uid, avs::BufferView> bufferViews;
	std::map<avs::uid, avs::GeometryBuffer> geometryBuffers;
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

bool GeometrySource::InitMesh(GeometrySource::Mesh *m, FStaticMeshLODResources &lod)
{
	m->primitiveArrays.resize(lod.Sections.Num());
	for (size_t i = 0; i < m->primitiveArrays.size(); i++)
	{
		auto &section = lod.Sections[i];
		FPositionVertexBuffer &pb = lod.VertexBuffers.PositionVertexBuffer;
		FStaticMeshVertexBuffer &vb = lod.VertexBuffers.StaticMeshVertexBuffer;
		auto &pa = m->primitiveArrays[i];
		pa.attributeCount = 2 + vb.GetTangentData() ? 1 : 0 + vb.GetTexCoordData() ? vb.GetNumTexCoords() : 0;
		pa.attributes = new avs::Attribute[pa.attributeCount];
		auto AddBufferAndView = [this](GeometrySource::Mesh *m,size_t num,size_t stride,const void *data)
		{
			avs::BufferView &bv = m->bufferViews[a.bufferView];
			bv.byteOffset = 0;
			bv.byteLength = num * stride;
			bv.byteStride = stride;
			bv.buffer = avs::GenerateUid();
			avs::GeometryBuffer b = m->geometryBuffers[bv.buffer];
			b.byteLength = bv.byteLength;
			b.data = (const uint8_t *)data;			// Remember, just a pointer: we don't own this data.
		};
		size_t idx = 0;
		// Position:
		{
			avs::Attribute &attr = pa.attributes[idx++];
			attr.accessor = avs::GenerateUid();
			attr.semantic = avs::AttributeSemantic::POSITION;
			avs::Accessor &a = m->accessors(attr.accessor);
			a.byteOffset = 0;
			a.type = avs::Accessor::DataType::VEC3;
			a.componentType = avs::Accessor::ComponentType::FLOAT;
			a.count = pb.GetNumVertices();
			a.bufferView = avs::GenerateUid();
			AddBufferAndView(m,a.bufferView, pb.GetNumVertices(), pb.GetStride(), pb.GetVertexData());
		}
		// Normal:
		{
			avs::Attribute &attr = pa.attributes[idx++];
			attr.accessor = avs::GenerateUid();
			attr.semantic = avs::AttributeSemantic::NORMAL;
			avs::Accessor &a = m->accessors(attr.accessor);
			a.byteOffset = 0;
			a.type = avs::Accessor::DataType::VEC3;
			a.componentType = avs::Accessor::ComponentType::FLOAT;
			a.count = vb.GetNumVertices();// same as pb???
			a.bufferView = avs::GenerateUid();
			AddBufferAndView(m, a.bufferView, vb.GetNumVertices(), vb.GetStride(), vb.GetTangentData());
		}
		if(vb.GetTangentData())
		{
			avs::Attribute &attr = pa.attributes[idx++];
			attr.accessor = avs::GenerateUid();
			attr.semantic = avs::AttributeSemantic::TANGENT;
			avs::Accessor &a = m->accessors(attr.accessor);
			a.byteOffset = 0;
			a.type = avs::Accessor::DataType::VEC4;
			a.componentType = avs::Accessor::ComponentType::FLOAT;
			a.count = vb.GetTangentSize();// same as pb???
			a.bufferView = avs::GenerateUid();
			AddBufferAndView(m, a.bufferView, vb.GetNumVertices(), vb.GetStride(), vb.GetTangentData());
		}
		for (size_t j = 0; j < vb.GetNumTexCoords; j++)
		{
			avs::Attribute &attr = pa.attributes[idx++];
			attr.accessor = avs::GenerateUid();
			attr.semantic = j==0?avs::AttributeSemantic::TEXCOORD_0: avs::AttributeSemantic::TEXCOORD_1;
			avs::Accessor &a = m->accessors(attr.accessor);
			a.byteOffset = 0;
			a.type = avs::Accessor::DataType::VEC4;
			a.componentType = avs::Accessor::ComponentType::FLOAT;
			a.count = vb.GetTangentSize();// same as pb???
			a.bufferView = avs::GenerateUid();
			AddBufferAndView(m, a.bufferView, vb.GetNumVertices(), vb.GetStride(), vb.GetTangentData());
			idx++;
		}
		pa.indices_accessor = avs::GenerateUid();
		pa.material = avs::GenerateUid();
		pa.primitiveMode = avs::PrimitiveMode::TRIANGLES;
	}
}

GeometrySource::GeometrySource()
{
}

GeometrySource::~GeometrySource()
{
	Meshes.Empty();
	GeometryInstances.Empty();
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
	avs::uid node_uid;
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

size_t GeometrySource::getNodeCount() const
{
	return size_t();
}

avs::uid GeometrySource::getNodeUid(size_t index) const
{
	return avs::uid();
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
	auto &lods=staticMesh->RenderData->LODResources;
	if (!lods.Num())
		return 0;
	return lods[0].Sections.Num();
}


bool GeometrySource::getMeshPrimitiveArray(avs::uid mesh_uid, size_t array_index, avs::PrimitiveArray & primitiveArray) const
{
	auto &mesh = Meshes[mesh_uid];
	UStaticMesh *staticMesh = mesh->StaticMesh;
	auto &lods = staticMesh->RenderData->LODResources;
	if (!lods.Num())
		return false;
	auto &lod=lods[0];
	if (!mesh->primitiveArrays.size())
	{
		InitMesh(mesh, lod);
	}
	primitiveArray.attributes;
	section.
	return false;
}

bool GeometrySource::getAccessor(avs::uid accessor_uid, avs::Accessor & accessor) const
{
	return false;
}

bool GeometrySource::getBufferView(avs::uid buffer_view_uid, avs::BufferView & bufferView) const
{
	return false;
}

bool GeometrySource::getBuffer(avs::uid buffer_uid, avs::GeometryBuffer & buffer) const
{
	return false;
}
