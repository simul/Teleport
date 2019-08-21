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
 
#pragma optimize("",off)

FGeometryStreamingService::FGeometryStreamingService()
{
}

FGeometryStreamingService::~FGeometryStreamingService()
{
	if(avsPipeline)	
		avsPipeline->deconfigure();
}

void FGeometryStreamingService::StartStreaming(UWorld* World, GeometrySource *geomSource,FRemotePlayContext* Context)
{
	if (RemotePlayContext == Context)
		return;
	if (!geomSource)
	{
		return;
	}
	geometrySource = geomSource;
	RemotePlayContext = Context;
	 
	avsPipeline.Reset(new avs::Pipeline); 
	avsGeometrySource.Reset(new avs::GeometrySource);
	avsGeometryEncoder.Reset(new avs::GeometryEncoder);
	avsGeometrySource->configure(geometrySource,this);
	avsGeometryEncoder->configure(&geometryEncoder);

	avsPipeline->link({ avsGeometrySource.Get(), avsGeometryEncoder.Get(), RemotePlayContext->GeometryQueue.Get() });

	// It is intended that each session component should track the streamed actors that enter its bubble.
	// These should be disposed of when they move out of range.
	TArray<AActor*> GSActors;
	UGameplayStatics::GetAllActorsOfClass(World, AStaticMeshActor::StaticClass(), GSActors);
	for (auto actor : GSActors)
	{
		auto c = actor->GetComponentByClass(UStreamableGeometryComponent::StaticClass());
		if (!c)
			continue;

		UStreamableGeometryComponent *geometryComponent = static_cast<UStreamableGeometryComponent*>(c);
		AddNode(geometryComponent->GetMesh());
		
		//Add material, and textures, for streaming to clients.
		geometrySource->AddMaterial(geometryComponent);
	}
}

avs::uid FGeometryStreamingService::AddNode(UMeshComponent* component)
{
	avs::uid mesh_uid = geometrySource->AddStreamableMeshComponent(component);

	avs::uid node_uid = geometrySource->CreateNode(component->GetRelativeTransform(), mesh_uid, avs::NodeDataType::Mesh);
	std::shared_ptr<avs::DataNode> node;
	geometrySource->getNode(node_uid, node);

	TArray<USceneComponent*> children;
	component->GetChildrenComponents(false, children);

	TArray<UMeshComponent*> meshChildren;
	for (auto child : children)
	{
		if (child->GetClass()->IsChildOf(UMeshComponent::StaticClass()))
		{
			meshChildren.Add(Cast<UMeshComponent>(child));
		}
	}

	for (auto child : meshChildren)
	{
		node->childrenUids.push_back(AddNode(child));
	}

	return node_uid;
}

void FGeometryStreamingService::StopStreaming()
{ 
	if (geometrySource != nullptr)
	{
		geometrySource->clearData();
	}
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

bool FGeometryStreamingService::HasResource(avs::uid resource_uid) const
{
	///We need clientside to handshake when it is ready to receive payloads of resources.
	return false;
	//return sentResources.find(resource_uid) != sentResources.end() && sentResources.at(resource_uid) == true;
}

void FGeometryStreamingService::EncodedResource(avs::uid resource_uid)
{
	sentResources[resource_uid] = true;
}

void FGeometryStreamingService::RequestResource(avs::uid resource_uid)
{
	sentResources[resource_uid] = false;
}
