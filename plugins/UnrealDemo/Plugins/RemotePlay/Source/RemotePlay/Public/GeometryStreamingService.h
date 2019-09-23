// Copyright 2018 Simul.co

#pragma once

#include "CoreMinimal.h"
#include "GeometryEncoder.h"
#include "GeometrySource.h"
#include <libavstream/pipeline.hpp>

#include <unordered_map>

/*!
	A Geometry Streaming Service instance manages the streaming of geometry to a particular connected client. It
	is owned by the SessionComponent attached to the client's Pawn.

	The GSS has an INSTANCE of a Geometry Encoder, and a POINTER to the global Geometry Source.

	It has an avs::Pipeline, which uses the source and encoder as backends to send data to the global Geometry Queue.
*/
class FGeometryStreamingService : public avs::GeometryRequesterBackendInterface
{
public:
	FGeometryStreamingService();
	virtual ~FGeometryStreamingService();

	//avs::GeometryRequesterBackendInterface
	virtual bool HasResource(avs::uid resource_uid) const override;

	virtual void EncodedResource(avs::uid resource_uid) override;
	virtual void RequestResource(avs::uid resource_uid) override;

	virtual void GetResourcesClientNeeds(
		std::vector<avs::uid>& outMeshIds, 
		std::vector<avs::uid>& outTextureIds, 
		std::vector<avs::uid>& outMaterialIds,
		std::vector<avs::uid>& outShadowIds,
		std::vector<avs::uid>& outNodeIds) override;

	virtual avs::AxesStandard GetAxesStandard() const override
	{
		return RemotePlayContext->axesStandard;
	}

	inline void SetStreamingContinuously(bool val) { bStreamingContinuously = val; }

	void Initialise(UWorld *World, GeometrySource *geomSource);
	void StartStreaming(struct FRemotePlayContext *RemotePlayContext);
	//Stop streaming to a client.
	void StopStreaming();
	void Tick();

	//Reset GeometryStreamingService to default state.
	void Reset();

	//Add actor to be streamed to the client.
	//	newActor : Actor to be sent to the client.
	//Returns uid of the actor the client is now responsible for.
	avs::uid AddActor(AActor *newActor);
	//Remove actor from list of actors the client needs.
	//	oldActor : Actor to be removed from the list.
	//Returns uid of actor the client is no longer responsible for.
	avs::uid RemoveActor(AActor *oldActor);

	// avs::GeometryTransferState
	size_t getNumRequiredNodes() const;
	avs::uid getRequiredNode(size_t index) const;
private:
	struct FRemotePlayContext* RemotePlayContext;

	// The following MIGHT be moved later to a separate Pipeline class:
	TUniquePtr<avs::Pipeline> avsPipeline;
	TUniquePtr<avs::GeometrySource> avsGeometrySource;
	TUniquePtr<avs::GeometryEncoder> avsGeometryEncoder;

	GeometrySource *geometrySource;
	GeometryEncoder geometryEncoder;

	bool bStreamingContinuously = false;
	std::unordered_map<avs::uid, bool> sentResources; //Tracks the resources sent to the user; <resource identifier, doesClientHave>.
	std::map<FName, avs::uid> streamedActors; //Actors that the client needs to draw, and should be sent to them; <Level Unique Name, Node UID of root mesh>.

	//Recursive function to retrieve the resource UIDs from a node, and its child nodes.
	void GetNodeResourceUIDs(
		avs::uid nodeUID, 
		std::vector<avs::uid>& outMeshIds, 
		std::vector<avs::uid>& outTextureIds, 
		std::vector<avs::uid>& outMaterialIds, 
		std::vector<avs::uid>& outNodeIds);
};
