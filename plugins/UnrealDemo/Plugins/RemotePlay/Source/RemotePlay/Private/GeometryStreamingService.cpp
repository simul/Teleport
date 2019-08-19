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
		AddNode((Cast<UStreamableGeometryComponent>(c))->GetMesh());
	}
}

avs::uid FGeometryStreamingService::AddNode(UMeshComponent* component)
{
	auto mesh_uid = geometrySource->AddStreamableMeshComponent(component);

	auto node_uid = geometrySource->CreateNode(component->GetRelativeTransform(), mesh_uid, avs::NodeDataType::Mesh);
	auto node = geometrySource->getNode(node_uid);

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
