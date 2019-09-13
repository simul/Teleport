// Copyright 2018 Simul.co

#pragma once
#include "InputCoreTypes.h"
#include "Components/ActorComponent.h"
#include "RemotePlayDiscoveryService.h"
#include "StreamableGeometryComponent.generated.h"

class APawn;
class APlayerController;
class UTexture;

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

	//Returns the amount of materials used by this actor.
	int32 GetMaterialAmount();

	UStaticMeshComponent *GetMesh();
	//Returns the interface of the material used by the mesh.
	UMaterialInterface* GetMaterial(int32 materialIndex);
	//Returns all textures used in the material.
	TArray<UTexture*> GetUsedTextures();
	//Returns all textures from the property chain.
	//	materialProperty : Which property chain we are pulling the textures from.
	TArray<UTexture*> GetTextureChain(EMaterialProperty materialProperty);
	//Returns the first texture from the property chain.
	//	materialProperty : Which property chain we are pulling the texture from.
	UTexture* GetTexture(EMaterialProperty materialProperty);
	
	ULightComponent* GetLightComponent();
	TArray<UTexture2D*> GetLightAndShadowMaps();

private:
	TWeakObjectPtr<AActor> Actor;
	TWeakObjectPtr<UStaticMeshComponent> StaticMeshComponent;
	TWeakObjectPtr<ULightComponent> LightComponent;
	TArray<UTexture2D*> LightAndShadowMaps;

	EMaterialQualityLevel::Type textureQualityLevel; //Quality level to retrieve the textures.
	ERHIFeatureLevel::Type textureFeatureLevel; //Feature level to retrieve the textures.
};
