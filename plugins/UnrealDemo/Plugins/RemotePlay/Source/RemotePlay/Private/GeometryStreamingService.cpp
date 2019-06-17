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
	geometrySource = geomSource;
	RemotePlayContext = Context;
	 
	avsPipeline.Reset(new avs::Pipeline); 
	avsGeometrySource.Reset(new avs::GeometrySource);
	avsGeometryEncoder.Reset(new avs::GeometryEncoder);
	avsGeometrySource->configure(geometrySource,this);
	avsGeometryEncoder->configure(&geometryEncoder);

	avsPipeline->add(RemotePlayContext->GeometryQueue.Get());

	avsPipeline->link({ avsGeometrySource.Get(), avsGeometryEncoder.Get(), RemotePlayContext->GeometryQueue.Get() });

	// It is intended that each session component should track the streamed actors that enter its bubble.
	// These should be disposed of when they move out of range.
	TArray<AActor*> GSActors;
	UGameplayStatics::GetAllActorsOfClass(World, AStaticMeshActor::StaticClass(), GSActors);
	for (auto i : GSActors)
	{
		auto j = i->GetComponentByClass(UStreamableGeometryComponent::StaticClass());
		if (!j)
			continue;
		geometrySource->AddStreamableActor((UStreamableGeometryComponent*)(j));
	}
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
}
 
void FGeometryStreamingService::Tick()
{
	// Might not be initialized... YET
	//if (!avsPipeline)
		return;
	// This will be called by each streaming service, but only the first call per-frame really matters.
	geometrySource->Tick();
	// We can now be confident that all streamable geometries have been initialized, so we will do internal setup.
	// Each frame we manage a view of which streamable geometries should or shouldn't be rendered on our client.

	// For this client's POSITION and OTHER PROPERTIES,
	// Use the Geometry Source to determine which Node uid's are relevant.
	//geometrySource->
//	if(avsPipeline)
	//	avsPipeline->process();
}