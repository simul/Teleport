// Copyright 2018 Simul.co

#pragma once
#include "InputCoreTypes.h"
#include "Components/ActorComponent.h"
#include "RemotePlayDiscoveryService.h"
#include "StreamableGeometryComponent.generated.h"

class APawn;
class APlayerController;

typedef struct _ENetHost   ENetHost;
typedef struct _ENetPeer   ENetPeer;
typedef struct _ENetPacket ENetPacket;
typedef struct _ENetEvent  ENetEvent;

// This is attached to each streamable actor.
// The mesh may be shared.
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class REMOTEPLAY_API UStreamableGeometryComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UStreamableGeometryComponent();
	
	/* Begin UActorComponent interface */
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	/* End UActorComponent interface */

	UStaticMeshComponent *GetMesh();


private:
	TWeakObjectPtr<AActor> Actor;
	TWeakObjectPtr<UStaticMeshComponent> StaticMeshComponent;
};
