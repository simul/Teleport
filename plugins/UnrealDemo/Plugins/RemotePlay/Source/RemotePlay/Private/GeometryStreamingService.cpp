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

	geometrySource->Initialize(ARemotePlayMonitor::Instantiate(World), World);

	//Decompose all relevant actors into streamable geometry.
	TArray<AActor*> staticMeshActors;
	UGameplayStatics::GetAllActorsOfClass(World, AStaticMeshActor::StaticClass(), staticMeshActors);

	ECollisionChannel remotePlayChannel;
	FCollisionResponseParams profileResponses;
	//Returns the collision channel used by RemotePlay; uses the object type of the profile to determine the channel.
	UCollisionProfile::GetChannelAndResponseParams("RemotePlaySensor", remotePlayChannel, profileResponses);

	avs::uid root_node_uid = geometrySource->GetRootNodeUid();

	for(auto actor : staticMeshActors)
	{
		UMeshComponent *rootMesh = Cast<UMeshComponent>(actor->GetComponentByClass(UMeshComponent::StaticClass()));
		
		//Decompose the meshes that would cause an overlap event to occur with the "RemotePlaySensor" profile.
		if(rootMesh->GetGenerateOverlapEvents() && rootMesh->GetCollisionResponseToChannel(remotePlayChannel) != ECollisionResponse::ECR_Ignore)
		{
			geometrySource->AddNode(root_node_uid, rootMesh, true);
		}
	}

	//Decompose all relevant light actors into streamable geometry.
	TArray<AActor*> lightActors;
	UGameplayStatics::GetAllActorsOfClass(World, ALight::StaticClass(), lightActors);

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
				geometrySource->AddNode(root_node_uid, lightComponent, true);
			}
		}
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
	streamedActors.clear();
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
	
	if(avsPipeline)
		avsPipeline->process();
}

void FGeometryStreamingService::Reset()
{
	sentResources.clear();
	streamedActors.clear();
}

avs::uid FGeometryStreamingService::AddActor(AActor *newActor)
{
	avs::uid actor_uid = geometrySource->AddNode(geometrySource->GetRootNodeUid(), Cast<UMeshComponent>(newActor->GetComponentByClass(UMeshComponent::StaticClass())));
	if (!actor_uid)
		return actor_uid;
	streamedActors[GetLevelUniqueActorName(newActor)] = actor_uid;

	return actor_uid;
}

avs::uid FGeometryStreamingService::RemoveActor(AActor *oldActor)
{
	FName levelUniqueName = GetLevelUniqueActorName(oldActor);
	if (streamedActors.find(levelUniqueName) == streamedActors.end())
		return 0;
	avs::uid actor_uid = streamedActors[levelUniqueName];
	streamedActors.erase(levelUniqueName);

	return actor_uid;
}

void FGeometryStreamingService::AddControllersToStream()
{
	const std::vector<avs::uid>& handUIDs = geometrySource->GetHandActorUIDs();

	if(handUIDs.size() != 0)
	{
		streamedActors["RemotePlayHandActor1"] = handUIDs[0];
		streamedActors["RemotePlayHandActor2"] = handUIDs[1];
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
	std::shared_ptr<avs::DataNode> thisNode;
	geometrySource->getNode(nodeUID, thisNode);

	//Add mesh UID.
	outMeshIds.push_back(thisNode->data_uid);

	//Add all material UIDs.
	for(auto materialId : thisNode->materials)
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
	for(auto childNode : thisNode->childrenUids)
	{
		outNodeIds.push_back(childNode);
		GetNodeResourceUIDs(childNode, outMeshIds, outTextureIds, outMaterialIds, outNodeIds);
	}
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

void FGeometryStreamingService::GetResourcesClientNeeds(
	std::vector<avs::uid>& outMeshIds, 
	std::vector<avs::uid>& outTextureIds, 
	std::vector<avs::uid>& outMaterialIds,
	std::vector<avs::uid>& outShadowIds,
	std::vector<avs::uid>& outNodeIds)
{
	outMeshIds.empty();
	outMaterialIds.empty();
	outShadowIds.empty();
	outNodeIds.empty();

	for(auto nodePair : streamedActors)
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
			if (node.second->data_uid == shadowUID)
			{
				outNodeIds.push_back(node.first);
				break;
			}
		}
	}

	//Remove duplicates, and 0s, from mesh IDs.
	std::sort(outMeshIds.begin(), outMeshIds.end());
	outMeshIds.erase(std::unique(outMeshIds.begin(), outMeshIds.end()), outMeshIds.end());
	outMeshIds.erase(std::remove(outMeshIds.begin(), outMeshIds.end(), 0), outMeshIds.end());

	//Remove duplicates, and 0s, from texture IDs.
	std::sort(outTextureIds.begin(), outTextureIds.end());
	outTextureIds.erase(std::unique(outTextureIds.begin(), outTextureIds.end()), outTextureIds.end());
	outTextureIds.erase(std::remove(outTextureIds.begin(), outTextureIds.end(), 0), outTextureIds.end());

	//Remove duplicates, and 0s, from material IDs.
	std::sort(outMaterialIds.begin(), outMaterialIds.end());
	outMaterialIds.erase(std::unique(outMaterialIds.begin(), outMaterialIds.end()), outMaterialIds.end());
	outMaterialIds.erase(std::remove(outMaterialIds.begin(), outMaterialIds.end(), 0), outMaterialIds.end()); ///Do Nodes contain 0 material ids?
	
	//Remove duplicates, and 0s, from shadow IDs.
	std::sort(outShadowIds.begin(), outShadowIds.end());
	outShadowIds.erase(std::unique(outShadowIds.begin(), outShadowIds.end()), outShadowIds.end());
	outShadowIds.erase(std::remove(outShadowIds.begin(), outShadowIds.end(), 0), outShadowIds.end());
}
