// Copyright 2018 Simul.co
#define WIN32_LEAN_AND_MEAN

#include "GeometryStreamingService.h"

#include <algorithm>

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

FName GetLevelUniqueActorName(AActor* actor)
{
	return *FPaths::Combine(actor->GetOutermost()->GetName(), actor->GetName());
}

FGeometryStreamingService::~FGeometryStreamingService()
{
	if(avsPipeline)	
		avsPipeline->deconfigure();
}

void FGeometryStreamingService::Initialise(UWorld *World, GeometrySource* source)
{
	if(!source) return;
	geometrySource = source;

	requester.initialise(&geometrySource->GetStorage());

	TArray<AActor*> staticMeshActors;
	UGameplayStatics::GetAllActorsOfClass(World, AStaticMeshActor::StaticClass(), staticMeshActors);

	TArray<AActor*> lightActors;
	UGameplayStatics::GetAllActorsOfClass(World, ALight::StaticClass(), lightActors);

	ECollisionChannel remotePlayChannel;
	FCollisionResponseParams profileResponses;
	//Returns the collision channel used by RemotePlay; uses the object type of the profile to determine the channel.
	UCollisionProfile::GetChannelAndResponseParams("RemotePlaySensor", remotePlayChannel, profileResponses);

	Monitor = ARemotePlayMonitor::Instantiate(World);
	geometrySource->Initialise(Monitor, World);

	//Decompose all relevant actors into streamable geometry.
	for(auto actor : staticMeshActors)
	{
		UMeshComponent *rootMesh = Cast<UMeshComponent>(actor->GetComponentByClass(UMeshComponent::StaticClass()));
		
		//Decompose the meshes that would cause an overlap event to occur with the "RemotePlaySensor" profile.
		if(rootMesh->GetGenerateOverlapEvents() && rootMesh->GetCollisionResponseToChannel(remotePlayChannel) != ECollisionResponse::ECR_Ignore)
		{
			geometrySource->AddNode(rootMesh);
		}
	}

	//Decompose all relevant light actors into streamable geometry.
	for (auto actor : lightActors)
	{
		auto sgc = actor->GetComponentByClass(UStreamableGeometryComponent::StaticClass());
		if (sgc)
		{
			//TArray<UTexture2D*> shadowAndLightMaps = static_cast<UStreamableGeometryComponent*>(sgc)->GetLightAndShadowMaps();
			ULightComponent* lightComponent = static_cast<UStreamableGeometryComponent*>(sgc)->GetLightComponent();
			if (lightComponent)
			{
				//ShadowMapData smd(lc);
				geometrySource->AddNode(lightComponent);
			}
		}
	}

	geometrySource->CompressTextures();
}

void FGeometryStreamingService::StartStreaming(FRemotePlayContext *Context)
{
	if (RemotePlayContext == Context) return;

	RemotePlayContext = Context;

	avsPipeline.Reset(new avs::Pipeline); 
	avsGeometrySource.Reset(new avs::GeometrySource);
	avsGeometryEncoder.Reset(new avs::GeometryEncoder);
	avsGeometrySource->configure(&geometrySource->GetStorage(), &requester);
	avsGeometryEncoder->configure(&geometryEncoder);

	avsPipeline->link({ avsGeometrySource.Get(), avsGeometryEncoder.Get(), RemotePlayContext->GeometryQueue.Get() });
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

	requester.reset();

	actorIDs.clear();
	streamedActors.clear();

	for(auto i : hiddenActors)
	{
		if(i.second)
			i.second->SetActorHiddenInGame(false);
	}
	hiddenActors.clear();
}
 
void FGeometryStreamingService::Tick(float DeltaTime)
{
	// Might not be initialized... YET
	if (!avsPipeline)
		return;

	geometryEncoder.geometryBufferCutoffSize = Monitor->GeometryBufferCutoffSize;
	requester.confirmationWaitTime = Monitor->ConfirmationWaitTime;

	// We can now be confident that all streamable geometries have been initialized, so we will do internal setup.
	// Each frame we manage a view of which streamable geometries should or shouldn't be rendered on our client.

	requester.tick(DeltaTime);

	// For this client's POSITION and OTHER PROPERTIES,
	// Use the Geometry Source to determine which Node uid's are relevant.
	
	avsPipeline->process();
}

void FGeometryStreamingService::Reset()
{
	requester.reset();

	actorIDs.clear();
	streamedActors.clear();
	hiddenActors.clear();
}

void FGeometryStreamingService::HideActor(avs::uid actorID)
{
	auto actorPair = streamedActors.find(actorID);
	if(actorPair != streamedActors.end())
	{
		actorPair->second->SetActorHiddenInGame(true);
		hiddenActors[actorID] = actorPair->second;
	}
	else
	{
		UE_LOG(LogRemotePlay, Warning, TEXT("Tried to hide non-streamed actor with UID of %d!"), actorID);
	}
}

void FGeometryStreamingService::ShowActor(avs::uid actorID)
{
	auto actorPair = hiddenActors.find(actorID);
	if(actorPair != hiddenActors.end())
	{
		actorPair->second->SetActorHiddenInGame(false);
		hiddenActors.erase(actorPair);
	}
	else
	{
		UE_LOG(LogRemotePlay, Warning, TEXT("Tried to show non-hidden actor with UID of %d!"), actorID);
	}
}

void FGeometryStreamingService::SetActorVisible(avs::uid actorID, bool isVisible)
{
	if(isVisible) ShowActor(actorID);
	else HideActor(actorID);
}

avs::uid FGeometryStreamingService::AddActor(AActor *newActor)
{
	UActorComponent* meshComponent = newActor->GetComponentByClass(UMeshComponent::StaticClass());
	if(!meshComponent)
	{
		UE_LOG(LogRemotePlay, Warning, TEXT("Failed to find mesh component when adding actor \"%s\" to geometry stream."), *newActor->GetName());
		return 0;
	}

	avs::uid actorID = geometrySource->GetNode(Cast<UMeshComponent>(meshComponent));
	
	if(actorID != 0)
	{
		actorIDs[GetLevelUniqueActorName(newActor)] = actorID;
		streamedActors[actorID] = newActor;

		requester.startStreamingActor(actorID);
	}

	return actorID;
}

avs::uid FGeometryStreamingService::RemoveActor(AActor* oldActor)
{
	FName levelUniqueName = GetLevelUniqueActorName(oldActor);

	avs::uid actorID = actorIDs[levelUniqueName];
	actorIDs.erase(levelUniqueName);
	streamedActors.erase(actorID);

	requester.stopStreamingActor(actorID);

	return actorID;
}

avs::uid FGeometryStreamingService::GetActorID(AActor* actor)
{
	auto idPair = actorIDs.find(GetLevelUniqueActorName(actor));

	return idPair != actorIDs.end() ? idPair->second : 0;
}

bool FGeometryStreamingService::IsStreamingActor(AActor* actor)
{
	return actorIDs.find(GetLevelUniqueActorName(actor)) != actorIDs.end();
}