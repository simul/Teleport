// Copyright 2018 Simul.co

#pragma once

#include <unordered_map>
#include <memory>

#include "SimulCasterServer/GeometryStreamingService.h"

#include "GeometrySource.h"

namespace SCServer
{
	struct CasterContext;
}

/*!
	A Geometry Streaming Service instance manages the streaming of geometry to a particular connected client. It
	is owned by the SessionComponent attached to the client's Pawn.

	The GSS has an INSTANCE of a Geometry Encoder, and a POINTER to the global Geometry Source.

	It has an avs::Pipeline, which uses the source and encoder as backends to send data to the global Geometry Queue.
*/
class FGeometryStreamingService : public SCServer::GeometryStreamingService
{
public:
	FGeometryStreamingService();
	virtual ~FGeometryStreamingService() = default;

	void initialise(GeometrySource* source);

	//Add actor to be streamed to the client.
	//	newActor : Actor to be sent to the client.
	//Returns uid of the actor the client is now responsible for, or 0 if the actor is not supported.
	avs::uid addActor(AActor *newActor);
	//Remove actor from list of actors the client needs.
	//	oldActor : Actor to be removed from the list.
	//Returns uid of actor the client is no longer responsible for, or 0 if the actor was never being streamed.
	avs::uid removeActor(AActor* oldActor);

	avs::uid getActorID(AActor* actor);

	bool isStreamingActor(AActor* actor);
private:
	GeometrySource* geometrySource;

	virtual void showActor_Internal(avs::uid clientID,void* actorPtr) override;
	virtual void hideActor_Internal(avs::uid clientID,void* actorPtr) override;
};