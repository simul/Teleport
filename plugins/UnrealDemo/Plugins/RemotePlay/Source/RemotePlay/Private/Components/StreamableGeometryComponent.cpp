// Copyright 2018 Simul.co
#define WIN32_LEAN_AND_MEAN

#include "Components/StreamableGeometryComponent.h"
#include "RemotePlayModule.h"

#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

#include "enet/enet.h"


UStreamableGeometryComponent::UStreamableGeometryComponent()
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

UStaticMeshComponent *UStreamableGeometryComponent::GetMesh()
{
	return StaticMeshComponent.Get();
}
