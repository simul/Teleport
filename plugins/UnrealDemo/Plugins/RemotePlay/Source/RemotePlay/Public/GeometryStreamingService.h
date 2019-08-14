// Copyright 2018 Simul.co

#pragma once

#include "CoreMinimal.h"
#include "GeometryEncoder.h"
#include "GeometrySource.h"
#include <libavstream/pipeline.hpp>

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

	bool hasMesh(avs::uid mesh_uid) const override
	{
		return false;
	}

	virtual bool hasTexture(avs::uid texture_uid) const override
	{
		return false;
	}

	virtual bool hasMaterial(avs::uid material_uid) const
	{
		return false;
	}
};
