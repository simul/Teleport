// Copyright 2018 Simul.co
#define WIN32_LEAN_AND_MEAN

#include "GeometryStreamingService.h"
#include "Components/StreamableGeometryComponent.h"
#include "RemotePlayContext.h"

#pragma optimize("",off)

struct FGeometryStreamingService::Mesh : public avs::MeshBackendInterface
{
	virtual void deconfigure()
	{
		if(InputMesh)
			InputMesh->deconfigure();
		if(GeometryEncoder)
			GeometryEncoder->deconfigure();
		InputMesh.Reset();
		GeometryEncoder.Reset();
	}
	class UStaticMesh* StaticMesh;
	unsigned long long SentFrame;
	bool Confirmed;
	TUniquePtr<avs::Mesh> InputMesh;
	TUniquePtr<avs::GeometryEncoder> GeometryEncoder;
	
	TArray<uint32> Indices;
	TArray<FVector> VertexPositions;
	size_t getNumVertices() const override
	{
		return VertexPositions.Num();
	}
	size_t getNumFaces() const override
	{
		return Indices.Num()/3;
	}
	const unsigned int *getFace(unsigned int index) const override
	{
		return Indices.GetData() + index * 3;
	}
	const float *getVertex(unsigned int index) const override
	{
		return (const float *)(&VertexPositions[index]);
	}
};

FGeometryStreamingService::FGeometryStreamingService()
{
}

FGeometryStreamingService::~FGeometryStreamingService()
{
	if(Pipeline)	
		Pipeline->deconfigure();
	ResetCache();
}

void FGeometryStreamingService::ResetCache()
{
	for (auto &m : Meshes)
	{
		m->deconfigure();
		m.Reset();
	}
	Meshes.Empty();
	GeometryInstances.Empty();
}

void FGeometryStreamingService::StartStreaming(FRemotePlayContext* Context)
{
	if (RemotePlayContext == Context)
		return;
	RemotePlayContext = Context;
	 
	Pipeline.Reset(new avs::Pipeline); 
	Pipeline->add(RemotePlayContext->GeometryQueue.Get());
}
 
void FGeometryStreamingService::StopStreaming()
{ 
	if(Pipeline)
		Pipeline->deconfigure();
	Pipeline.Reset();
	RemotePlayContext = nullptr;
}
 
void FGeometryStreamingService::Tick()
{
	// Might not be initialized... YET
	if (!Pipeline)
		return;
	// We can now be confident that all streamable geometries have been initialized, so we will do internal setup.
	// Each frame we manage a view of which streamable geometries should or shouldn't be rendered on our client.
	for (auto a : ToAdd)
	{
		AddInternal(a);
	}
	ToAdd.Empty();
	for (auto &m : Meshes)
	{
		if (!m->Confirmed&&(GFrameNumber-m->SentFrame)>100)
		{
			SendMesh(*m);
			m->SentFrame = GFrameNumber;
		}
	}
	if(Pipeline)
		Pipeline->process();
} 
  
void FGeometryStreamingService::SendMesh(Mesh  &m) 
{
	RemotePlayContext->GeometryQueue; 
}

void FGeometryStreamingService::Add(UStreamableGeometryComponent *StreamableGeometryComponent)
{
	ToAdd.AddUnique(StreamableGeometryComponent);
} 
 
// By adding a mesh, we also add a pipe, including the InputMesh, which must be configured with the appropriate 
void FGeometryStreamingService::AddMeshInternal(UStaticMesh *StaticMesh) 
{
	Meshes.Add(TSharedPtr<Mesh>(new Mesh));
	TSharedPtr<Mesh> &mesh = Meshes.Last();
	mesh->InputMesh.Reset(new avs::Mesh);
	mesh->InputMesh->configure(mesh.Get());
	mesh->GeometryEncoder.Reset( new avs::GeometryEncoder);
	mesh->GeometryEncoder->configure();
	mesh->StaticMesh = StaticMesh;
	mesh->SentFrame = (unsigned long long)0;
	mesh->Confirmed = false;
	PrepareMesh(*mesh);
	Pipeline->link({ mesh->InputMesh.Get(), mesh->GeometryEncoder.Get() });
	avs::Node::link(*mesh->GeometryEncoder, *RemotePlayContext->GeometryQueue);
#ifdef DISABLE
#endif
}

void FGeometryStreamingService::AddInternal(UStreamableGeometryComponent *StreamableGeometryComponent)
{
	UStaticMesh *StaticMesh = StreamableGeometryComponent->GetMesh()->GetStaticMesh();
	bool already_got_mesh = false;
	for (auto &i : Meshes)
	{
		if (i->StaticMesh== StaticMesh)
			already_got_mesh = true;
	}
	if (!already_got_mesh)
	{
		AddMeshInternal(StaticMesh);
	}
	GeometryInstance geom = { StreamableGeometryComponent,0 };
	bool already_got_geom = false;
	for (auto i : GeometryInstances)
	{
		if (i.Geometry == geom.Geometry)
			already_got_geom = true;
	}
	if(!already_got_geom)
		GeometryInstances.Add(geom);
#ifdef DISABLE
#endif
}

void FGeometryStreamingService::PrepareMesh(Mesh &m)
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

	FPositionVertexBuffer &PositionVertexBuffer = LODResources.VertexBuffers.PositionVertexBuffer;
	FStaticMeshVertexBuffer &StaticMeshVertexBuffer = LODResources.VertexBuffers.StaticMeshVertexBuffer;

	uint32 pos_stride = PositionVertexBuffer.GetStride();
	const float *pos_data = (const float*)PositionVertexBuffer.GetVertexData();

	int numVertices = PositionVertexBuffer.GetNumVertices();
	m.VertexPositions.Reset(numVertices);
	for (int i = 0; i < numVertices; i++)
	{
		m.VertexPositions.Add(FVector(pos_data[0]
			, pos_data[1]
			, pos_data[2]));
		pos_data += pos_stride / sizeof(float);
	}
	LODResources.IndexBuffer.GetCopy(m.Indices);
#ifdef DISABLE
#endif
}
