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

FName GetLevelUniqueActorName(AActor* actor)
{
	return *FPaths::Combine(actor->GetOutermost()->GetName(), actor->GetName());
}

//Remove duplicates, and 0s, from passed vector of UIDs..
void UniqueUIDsOnly(std::vector<avs::uid>& cleanedUIDs)
{
	std::sort(cleanedUIDs.begin(), cleanedUIDs.end());
	cleanedUIDs.erase(std::unique(cleanedUIDs.begin(), cleanedUIDs.end()), cleanedUIDs.end());
	cleanedUIDs.erase(std::remove(cleanedUIDs.begin(), cleanedUIDs.end(), 0), cleanedUIDs.end());
}

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
	geometryEncoder.Initialise(Monitor);

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
	avsGeometrySource->configure(geometrySource,this);
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

	sentResources.clear();
	actorUids.clear();
	for (auto i : hiddenActors)
	{
		if (i.second)
			i.second->SetActorHiddenInGame(false);
	}
	streamedActors.clear();
	hiddenActors.clear();
}
 
void FGeometryStreamingService::Tick(float DeltaTime)
{
	// Might not be initialized... YET
	if (!avsPipeline)
		return;
	// This will be called by each streaming service, but only the first call per-frame really matters.
	geometrySource->Tick();
	// We can now be confident that all streamable geometries have been initialized, so we will do internal setup.
	// Each frame we manage a view of which streamable geometries should or shouldn't be rendered on our client.

	//Increment time for unconfirmed resources, if they pass the max time then they are flagged to be sent again.
	for(auto it = unconfirmedResourceTimes.begin(); it != unconfirmedResourceTimes.end(); it++)
	{
		it->second += DeltaTime;

		if(it->second > Monitor->ConfirmationWaitTime)
		{
			UE_LOG(LogRemotePlay, Log, TEXT("Resource with UID %llu was not confirmed within %.2f seconds, and will be resent."), it->first, Monitor->ConfirmationWaitTime);

			sentResources[it->first] = false;
			it = unconfirmedResourceTimes.erase(it);
		}
	}

	// For this client's POSITION and OTHER PROPERTIES,
	// Use the Geometry Source to determine which Node uid's are relevant.
	
	avsPipeline->process();
}

void FGeometryStreamingService::Reset()
{
	sentResources.clear();
	streamedActors.clear();
	actorUids.clear();
}

void FGeometryStreamingService::SetShowClientSideActor(avs::uid actor_uid,bool show)
{
	auto actorPair = streamedActors.find(actor_uid);
	if(actorPair != streamedActors.end())
	{
		actorPair->second->SetActorHiddenInGame(!show);
	}
	else
	{
		UE_LOG(LogRemotePlay, Warning, TEXT("Tried to %s non-streamed actor with UID of %d!"), show ? TEXT("show") : TEXT("hide"), actor_uid);
	}
}

void FGeometryStreamingService::HideActor(avs::uid actor_uid)
{
	auto actorPair = streamedActors.find(actor_uid);
	if(actorPair != streamedActors.end())
	{
		actorPair->second->SetActorHiddenInGame(true);
		hiddenActors[actor_uid] = actorPair->second;
	}
	else
	{
		UE_LOG(LogRemotePlay, Warning, TEXT("Tried to hide non-streamed actor with UID of %d!"), actor_uid);
	}
}

void FGeometryStreamingService::ShowActor(avs::uid actor_uid)
{
	auto actorPair = hiddenActors.find(actor_uid);
	if(actorPair != hiddenActors.end())
	{
		actorPair->second->SetActorHiddenInGame(false);
		hiddenActors.erase(actorPair);
	}
	else
	{
		UE_LOG(LogRemotePlay, Warning, TEXT("Tried to show non-hidden actor with UID of %d!"), actor_uid);
	}
}

avs::uid FGeometryStreamingService::AddActor(AActor *newActor)
{
	UActorComponent* meshComponent = newActor->GetComponentByClass(UMeshComponent::StaticClass());
	if(!meshComponent)
	{
		UE_LOG(LogRemotePlay, Warning, TEXT("Failed to find mesh when adding actor \"%s\" to geometry stream."), *newActor->GetName());
		return 0;
	}

	avs::uid actorID = geometrySource->GetNode(Cast<UMeshComponent>(meshComponent));
	
	if(actorID != 0)
	{
		actorUids[GetLevelUniqueActorName(newActor)] = actorID;
		streamedActors[actorID] = newActor;
	}

	return actorID;
}

avs::uid FGeometryStreamingService::RemoveActor(AActor* oldActor)
{
	FName levelUniqueName = GetLevelUniqueActorName(oldActor);

	avs::uid actor_uid = actorUids[levelUniqueName];
	actorUids.erase(levelUniqueName);
	streamedActors.erase(actor_uid);
	unconfirmedResourceTimes.erase(actor_uid);

	return actor_uid;
}

avs::uid FGeometryStreamingService::GetActorID(AActor* actor)
{
	auto idPair = actorUids.find(GetLevelUniqueActorName(actor));

	return idPair != actorUids.end() ? idPair->second : 0;
}

bool FGeometryStreamingService::IsStreamingActor(AActor* actor)
{
	return actorUids.find(GetLevelUniqueActorName(actor)) != actorUids.end();
}

void FGeometryStreamingService::AddControllersToStream()
{
	const std::vector<avs::uid>& handIDs = geometrySource->GetHandActorUIDs();

	if(handIDs.size() != 0)
	{
		actorUids["RemotePlayHandActor1"] = handIDs[0];
		actorUids["RemotePlayHandActor2"] = handIDs[1];
	
	}
}

void FGeometryStreamingService::GetNodeResourceUIDs(
	avs::uid nodeUID, 
	std::vector<avs::uid>& outMeshIds, 
	std::vector<avs::uid>& outTextureIds, 
	std::vector<avs::uid>& outMaterialIds,
	std::vector<avs::uid>& outNodeIds)
{
	//Retrieve node.
	avs::DataNode thisNode;
	geometrySource->getNode(nodeUID, thisNode);

	//Add mesh UID.
	outMeshIds.push_back(thisNode.data_uid);

	//Add all material UIDs.
	for(auto materialId : thisNode.materials)
	{
		avs::Material thisMaterial;
		geometrySource->getMaterial(materialId, thisMaterial);

		outTextureIds.insert(outTextureIds.end(),
			{
				thisMaterial.pbrMetallicRoughness.baseColorTexture.index,
				thisMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index,
				thisMaterial.normalTexture.index,
				thisMaterial.occlusionTexture.index,
				thisMaterial.emissiveTexture.index
			}
		);

		outMaterialIds.push_back(materialId);
	}

	//Add all child node UIDs.
	for(auto childNode : thisNode.childrenUids)
	{
		outNodeIds.push_back(childNode);
		GetNodeResourceUIDs(childNode, outMeshIds, outTextureIds, outMaterialIds, outNodeIds);
	}
}

void FGeometryStreamingService::GetMeshNodeResources(avs::uid node_uid, std::vector<avs::MeshNodeResources>& outMeshResources)
{
	avs::DataNode thisNode;
	geometrySource->getNode(node_uid, thisNode);
	if(thisNode.data_uid == 0) return;

	avs::MeshNodeResources meshNode;
	meshNode.node_uid = node_uid;
	meshNode.mesh_uid = thisNode.data_uid;

	for(avs::uid material_uid : thisNode.materials)
	{
		avs::Material thisMaterial;
		geometrySource->getMaterial(material_uid, thisMaterial);

		avs::MaterialResources material;
		material.material_uid = material_uid;

		material.texture_uids =
		{
			thisMaterial.pbrMetallicRoughness.baseColorTexture.index,
			thisMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index,
			thisMaterial.normalTexture.index,
			thisMaterial.occlusionTexture.index,
			thisMaterial.emissiveTexture.index
		};

		UniqueUIDsOnly(material.texture_uids);

		meshNode.materials.push_back(material);
	}

	for(avs::uid childNode_uid : thisNode.childrenUids)
	{
		GetMeshNodeResources(childNode_uid, outMeshResources);
	}

	outMeshResources.push_back(meshNode);
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
	unconfirmedResourceTimes[resource_uid] = 0;
}

void FGeometryStreamingService::RequestResource(avs::uid resource_uid)
{
	sentResources[resource_uid] = false;
	unconfirmedResourceTimes.erase(resource_uid);
}

void FGeometryStreamingService::ConfirmResource(avs::uid resource_uid)
{
	unconfirmedResourceTimes.erase(resource_uid);
	//Confirm again; incase something just elapsed the timer, but has yet to be sent.
	sentResources[resource_uid] = true;
}

void FGeometryStreamingService::GetResourcesClientNeeds(
	std::vector<avs::uid>& outMeshIds, 
	std::vector<avs::uid>& outTextureIds, 
	std::vector<avs::uid>& outMaterialIds,
	std::vector<avs::uid>& outShadowIds,
	std::vector<avs::uid>& outNodeIds)
{
	outMeshIds.empty();
	outTextureIds.empty();
	outMaterialIds.empty();
	outShadowIds.empty();
	outNodeIds.empty();

	for(auto nodePair : actorUids)
	{
		outNodeIds.push_back(nodePair.second);
		GetNodeResourceUIDs(nodePair.second, outMeshIds, outTextureIds, outMaterialIds, outNodeIds);
	}
	//shadowUIDs - Should replace this nested loop!
	outShadowIds = geometrySource->getShadowMapUIDs();
	auto& nodes = geometrySource->getNodes();
	for (auto& shadowUID : outShadowIds)
	{
		for (auto& node : nodes)
		{
			if (node.second.data_uid == shadowUID)
			{
				outNodeIds.push_back(node.first);
				break;
			}
		}
	}

	UniqueUIDsOnly(outMeshIds);
	UniqueUIDsOnly(outTextureIds);
	UniqueUIDsOnly(outMaterialIds);
}

void FGeometryStreamingService::GetResourcesToStream(std::vector<avs::MeshNodeResources>& outMeshResources, std::vector<avs::LightNodeResources>& outLightResources)
{
	for(auto nodePair : actorUids)
	{
		GetMeshNodeResources(nodePair.second, outMeshResources);
	}

	outLightResources = geometrySource->getLightNodes();
}
