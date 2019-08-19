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

	void StartStreaming(UWorld* World,GeometrySource *geometrySource,struct FRemotePlayContext* RemotePlayContext);
	void StopStreaming();
	void Tick();

	

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

	std::unordered_map<avs::uid, bool> sentResources; //Tracks the resources sent to the user; <resource identifier, doesClientHave>.

	avs::uid AddNode(class UMeshComponent* component);
};
