// Copyright 2018 Simul.co
#define WIN32_LEAN_AND_MEAN

#include "GeometryStreamingService.h"
#include "Components/StreamableGeometryComponent.h"
#include "RemotePlayContext.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/StaticMeshActor.h"
#include "Engine.h"
#include <libavstream/geometry/mesh_interface.hpp>
#include <libavstream/mesh.hpp>
#include <libavstream/geometryencoder.hpp>
 
#include "RemotePlayMonitor.h"

#pragma optimize("",off)

FGeometryStreamingService::FGeometryStreamingService()
{
}

FGeometryStreamingService::~FGeometryStreamingService()
{
	if(avsPipeline)	
		avsPipeline->deconfigure();
}

void FGeometryStreamingService::Initialise(UWorld *World, GeometrySource *geomSource)
{
	if(!geomSource)
	{
		return;
	}
	geometrySource = geomSource;

	geometrySource->Initialize(ARemotePlayMonitor::Instantiate(World));

	// It is intended that each session component should track the streamed actors that enter its bubble.
	// These should be disposed of when they move out of range.
	TArray<AActor*> GSActors;
	UGameplayStatics::GetAllActorsOfClass(World, AStaticMeshActor::StaticClass(), GSActors);

	avs::uid root_node_uid = geometrySource->GetRootNodeUid();

	for(auto actor : GSActors)
	{
		auto c = actor->GetComponentByClass(UStreamableGeometryComponent::StaticClass());
		if(!c)
			continue;

		UStreamableGeometryComponent* geometryComponent = static_cast<UStreamableGeometryComponent*>(c);
		AddNode(root_node_uid, geometryComponent->GetMesh());
	}
}

void FGeometryStreamingService::StartStreaming(FRemotePlayContext *Context)
{
	if (RemotePlayContext == Context) return;

	RemotePlayContext = Context;
	 
	avsPipeline.Reset(new avs::Pipeline); 
	avsGeometrySource.Reset(new avs::GeometrySource);
	avsGeometryEncoder.Reset(new avs::GeometryEncoder);
	avsGeometrySource->configure(geometrySource,this);
	avsGeometryEncoder->configure(&geometryEncoder);

	avsPipeline->link({ avsGeometrySource.Get(), avsGeometryEncoder.Get(), RemotePlayContext->GeometryQueue.Get() });
}

avs::uid FGeometryStreamingService::AddNode(avs::uid parent_uid, UMeshComponent* component)
{
	std::shared_ptr<avs::DataNode> parent;
	geometrySource->getNode(parent_uid, parent);

	avs::uid mesh_uid = geometrySource->AddStreamableMeshComponent(component);
	// the material/s that this particular instance of the mesh has applied to its slots...
	TArray<UMaterialInterface*> materials=component->GetMaterials();

	std::vector<avs::uid> mat_uids;
	//Add material, and textures, for streaming to clients.
	int32 num_mats = materials.Num();
	for (int32 i = 0; i < num_mats; i++)
	{
		UMaterialInterface *materialInterface = materials[i];
		mat_uids.push_back(geometrySource->AddMaterial(materialInterface));
	}

	avs::uid node_uid = geometrySource->CreateNode(component->GetRelativeTransform(), mesh_uid, avs::NodeDataType::Mesh, mat_uids);
	
	parent->childrenUids.push_back(node_uid);

	TArray<USceneComponent*> children;
	component->GetChildrenComponents(false, children);

	for (auto child : children)
	{
		if (child->GetClass()->IsChildOf(UMeshComponent::StaticClass()))
		{
			AddNode(node_uid, Cast<UMeshComponent>(child));
		}
	}
	return node_uid;
}

void FGeometryStreamingService::StopStreaming()
{ 
	if(avsPipeline)
		avsPipeline->deconfigure();
	if (avsGeometrySource)
		avsGeometrySource->deconfigure();
	if (avsGeometryEncoder)
		avsGeometryEncoder->deconfigure();
	avsPipeline.Reset();
	RemotePlayContext = nullptr;

	sentResources.clear();
}
 
void FGeometryStreamingService::Tick()
{
	// Might not be initialized... YET
	if (!avsPipeline)
		return;
	// This will be called by each streaming service, but only the first call per-frame really matters.
	geometrySource->Tick();
	// We can now be confident that all streamable geometries have been initialized, so we will do internal setup.
	// Each frame we manage a view of which streamable geometries should or shouldn't be rendered on our client.

	// For this client's POSITION and OTHER PROPERTIES,
	// Use the Geometry Source to determine which Node uid's are relevant.
	//geometrySource->
	if(avsPipeline)
		avsPipeline->process();
}

void FGeometryStreamingService::Reset()
{
	geometrySource->clearData();
}

bool FGeometryStreamingService::HasResource(avs::uid resource_uid) const
{
	///We need clientside to handshake when it is ready to receive payloads of resources.
	if(!bStreamingContinuously)
		return sentResources.find(resource_uid) != sentResources.end() && sentResources.at(resource_uid) == true;
	else
		return false;
}

void FGeometryStreamingService::EncodedResource(avs::uid resource_uid)
{
	sentResources[resource_uid] = true;
}

void FGeometryStreamingService::RequestResource(avs::uid resource_uid)
{
	sentResources[resource_uid] = false;
}
