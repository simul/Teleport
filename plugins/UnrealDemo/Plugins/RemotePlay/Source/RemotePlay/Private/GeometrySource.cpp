#include "GeometrySource.h"
#include "StreamableGeometryComponent.h"
#include "StaticMeshResources.h"
#if 0
#include <random>
std::default_random_engine generator;
std::uniform_int_distribution<int> distribution(1, 6);
int dice_roll = distribution(generator);
#endif
struct GeometrySource::Mesh
{
	class UStaticMesh* StaticMesh;
	unsigned long long SentFrame;
	bool Confirmed;
};

GeometrySource::GeometrySource()
{
}

GeometrySource::~GeometrySource()
{
	Meshes.Empty();
	GeometryInstances.Empty();
}

// By adding a mesh, we also add a pipe, including the InputMesh, which must be configured with the appropriate 
avs::uid GeometrySource::AddMesh(UStaticMesh *StaticMesh)
{
	avs::uid uid = avs::GenerateUid();
	TSharedPtr<Mesh> mesh ( new Mesh);
	Meshes.Add(uid, mesh);
	mesh->StaticMesh = StaticMesh;
	mesh->SentFrame = (unsigned long long)0;
	mesh->Confirmed = false;
	PrepareMesh(*mesh);
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
	return size_t();
}

avs::uid GeometrySource::getMeshUid(size_t index) const
{
	return avs::uid();
}

size_t GeometrySource::getMeshPrimitiveArrayCount(avs::uid mesh_uid) const
{
	return size_t();
}

bool GeometrySource::getMeshPrimitiveArray(avs::uid mesh_uid, size_t array_index, avs::PrimitiveArray & primitiveArray) const
{
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
