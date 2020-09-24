// Copyright 2018 Simul.co
#define WIN32_LEAN_AND_MEAN

#include "GeometryStreamingService.h"

#include "Engine/Classes/Components/MeshComponent.h"

#include "SimulCasterServer/CasterContext.h"

#include "RemotePlayMonitor.h"

#pragma optimize("", off)

FGeometryStreamingService::FGeometryStreamingService()
	:SCServer::GeometryStreamingService(ARemotePlayMonitor::GetCasterSettings())
{}

void FGeometryStreamingService::initialise(GeometrySource* source)
{
	if(!source) return;
	geometrySource = source;

	geometryStore = &geometrySource->GetStorage();
}

avs::uid FGeometryStreamingService::addActor(AActor* newActor)
{
	UActorComponent* meshComponent = newActor->GetComponentByClass(UMeshComponent::StaticClass());
	if(!meshComponent)
	{
		UE_LOG(LogRemotePlay, Warning, TEXT("Failed to find mesh component when adding actor \"%s\" to geometry stream."), *newActor->GetName());
		return 0;
	}

	avs::uid actorID = geometrySource->AddNode(Cast<UMeshComponent>(meshComponent));
	
	GeometryStreamingService::addActor(newActor, actorID);
	return actorID;
}

avs::uid FGeometryStreamingService::removeActor(AActor* oldActor)
{
	return GeometryStreamingService::removeActor(static_cast<void*>(oldActor));
}

avs::uid FGeometryStreamingService::getActorID(AActor* actor)
{
	return GeometryStreamingService::getActorID(actor);
}

bool FGeometryStreamingService::isStreamingActor(AActor* actor)
{
	return GeometryStreamingService::isStreamingActor(actor);
}

void FGeometryStreamingService::showActor_Internal(avs::uid clientID,void* actorPtr)
{
	AActor* actor = static_cast<AActor*>(actorPtr);
	//TODO: specific to client.
	actor->SetActorHiddenInGame(false);
}

void FGeometryStreamingService::hideActor_Internal(avs::uid clientID,void* actorPtr)
{
	AActor* actor = static_cast<AActor*>(actorPtr);
	actor->SetActorHiddenInGame(true);
}
