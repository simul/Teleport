// Copyright 2018 Simul.co

#pragma once

#include <unordered_map>

#include "GeometryEncoder.h"
#include "GeometryRequester.h"
#include "GeometrySource.h"

#include "libavstream/pipeline.hpp"

/*!
	A Geometry Streaming Service instance manages the streaming of geometry to a particular connected client. It
	is owned by the SessionComponent attached to the client's Pawn.

	The GSS has an INSTANCE of a Geometry Encoder, and a POINTER to the global Geometry Source.

	It has an avs::Pipeline, which uses the source and encoder as backends to send data to the global Geometry Queue.
*/
class FGeometryStreamingService
{
public:
	virtual ~FGeometryStreamingService();

	void Initialise(UWorld *World, GeometrySource* source);
	void StartStreaming(struct FRemotePlayContext *RemotePlayContext);
	//Stop streaming to a client.
	void StopStreaming();
	void Tick(float DeltaTime);

	//Reset GeometryStreamingService to default state.
	void Reset();

	//Add actor to be streamed to the client.
	//	newActor : Actor to be sent to the client.
	//Returns uid of the actor the client is now responsible for, or 0 if the actor is not supported.
	avs::uid AddActor(AActor *newActor);
	//Remove actor from list of actors the client needs.
	//	oldActor : Actor to be removed from the list.
	//Returns uid of actor the client is no longer responsible for, or 0 if the actor was never being streamed.
	avs::uid RemoveActor(AActor* oldActor);

	avs::uid GetActorID(AActor* actor);

	bool IsStreamingActor(AActor* actor);

	void HideActor(avs::uid actorID);
	void ShowActor(avs::uid actorID);
	void SetActorVisible(avs::uid actorID, bool isVisible);

	inline GeometryRequester& GetRequester()
	{
		return requester;
	}
private:
	struct FRemotePlayContext* RemotePlayContext;
	class ARemotePlayMonitor* Monitor;
	GeometryRequester requester;

	// The following MIGHT be moved later to a separate Pipeline class:
	TUniquePtr<avs::Pipeline> avsPipeline;
	TUniquePtr<avs::GeometrySource> avsGeometrySource;
	TUniquePtr<avs::GeometryEncoder> avsGeometryEncoder;

	GeometrySource *geometrySource;
	GeometryEncoder geometryEncoder;

	std::map<FName, avs::uid> actorIDs; //Actors that the client needs to draw, and should be sent to them; <Level Unique Name, Node ID of root mesh>.
	std::unordered_map<avs::uid, AActor*> streamedActors; //Actors that should be streamed to the client.
	std::unordered_map<avs::uid, AActor*> hiddenActors; //Actors that are currently hidden on the server.
};
