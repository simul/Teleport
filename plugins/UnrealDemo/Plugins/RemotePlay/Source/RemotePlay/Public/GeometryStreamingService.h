// Copyright 2018 Simul.co

#pragma once

#include "CoreMinimal.h"

/*!
	A Geometry Streaming Service instance manages the streaming of geometry to a particular connected client. It
	is owned by the SessionComponent attached to the client's Pawn.
*/
class FGeometryStreamingService
{
public:
	FGeometryStreamingService();
	~FGeometryStreamingService();

	void ResetCache();
	void StartStreaming(struct FRemotePlayContext* RemotePlayContext);
	void StopStreaming();
	void Tick();
	void Add(class UStreamableGeometryComponent*);

private:
	struct Mesh;

	struct GeometryInstance
	{
		class UStreamableGeometryComponent* Geometry;
		unsigned long long SentFrame;
	};
	TArray<TSharedPtr<Mesh>> Meshes;
	TArray<GeometryInstance> GeometryInstances;
	TArray< UStreamableGeometryComponent*> ToAdd;
	void AddMeshInternal(UStaticMesh *StaticMesh);
	void AddInternal(class UStreamableGeometryComponent*);
	void PrepareMesh(Mesh &m);
	void SendMesh(Mesh &m);
	struct FRemotePlayContext* RemotePlayContext;

	// The following MIGHT be moved later to a separate Pipeline class:
	TUniquePtr<avs::Pipeline> Pipeline;
};
