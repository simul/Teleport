// Copyright 2018 Simul.co
#define WIN32_LEAN_AND_MEAN

#include "Components/StreamableGeometryComponent.h"
#include "RemotePlayModule.h"

#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

#include "enet/enet.h"

#include "Engine/Classes/Materials/Material.h"


UStreamableGeometryComponent::UStreamableGeometryComponent()
	:textureQualityLevel(EMaterialQualityLevel::High),
	textureFeatureLevel(ERHIFeatureLevel::SM5)
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UStreamableGeometryComponent::BeginPlay()
{
	Super::BeginPlay();
	Actor = Cast<AActor>(GetOuter());
	StaticMeshComponent = Cast<UStaticMeshComponent>(Actor->GetComponentByClass(UStaticMeshComponent::StaticClass()));
}
	
void UStreamableGeometryComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	Super::EndPlay(Reason);
}
	
void UStreamableGeometryComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
}

int32 UStreamableGeometryComponent::GetMaterialAmount()
{
	return StaticMeshComponent->GetNumMaterials();
}

UStaticMeshComponent *UStreamableGeometryComponent::GetMesh()
{
	return StaticMeshComponent.Get();
}

UMaterialInterface * UStreamableGeometryComponent::GetMaterial(int32 materialIndex)
{
	return StaticMeshComponent->GetMaterial(materialIndex);
}

TArray<UTexture*> UStreamableGeometryComponent::GetUsedTextures()
{
	TArray<UTexture*> usedTextures;

	//WARNING: Always grabs material 0; doesn't account for multiple materials on a texture.
	UMaterialInterface *matInterface = GetMaterial(0);

	if(matInterface)
	{
		UMaterial* material = matInterface->GetMaterial();

		if(material)
		{
			material->GetUsedTextures(usedTextures, textureQualityLevel, false, textureFeatureLevel, false);
		}
	}

	return usedTextures;
}

TArray<UTexture*> UStreamableGeometryComponent::GetTextureChain(EMaterialProperty materialProperty)
{
	TArray<UTexture*> outTextures;

	//WARNING: Always grabs material 0; doesn't account for multiple materials on a texture.
	UMaterialInterface *matInterface = GetMaterial(0);

	if(matInterface)
	{
		matInterface->GetTexturesInPropertyChain(materialProperty, outTextures, nullptr, nullptr);
	}
	
	TArray<UTexture*> uniqueTextures;

	//Remove duplicates by moving unique instances to an array.
	while(outTextures.Num() != 0)
	{
		UTexture *uniqueTexture = outTextures[0];

		uniqueTextures.Add(uniqueTexture);
		//Removes all matching instances.
		outTextures.Remove(uniqueTexture);
	}

	return uniqueTextures;
}

UTexture * UStreamableGeometryComponent::GetTexture(EMaterialProperty materialProperty)
{
	TArray<UTexture*> outTextures;

	//WARNING: Always grabs material 0; doesn't account for multiple materials on a texture.
	UMaterialInterface *matInterface = GetMaterial(0);

	if(matInterface)
	{
		matInterface->GetTexturesInPropertyChain(materialProperty, outTextures, nullptr, nullptr);
	}

	UTexture * texture = nullptr;

	//Discarding duplicates by assuming we are using only one texture for the property chain.
	if(outTextures.Num() != 0)
	{
		texture = outTextures[0];
	}

	return texture;
}
