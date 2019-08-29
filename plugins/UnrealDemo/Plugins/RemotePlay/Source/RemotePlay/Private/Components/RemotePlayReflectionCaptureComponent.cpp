// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	
=============================================================================*/

#include "RemotePlayReflectionCaptureComponent.h"
//#include "Serialization/MemoryWriter.h"
//#include "UObject/RenderingObjectVersion.h"
//#include "UObject/ReflectionCaptureObjectVersion.h"
//#include "UObject/ConstructorHelpers.h"
//#include "GameFramework/Actor.h"
//#include "RHI.h"
//#include "RenderingThread.h"
//#include "RenderResource.h"
//#include "Misc/ScopeLock.h"
//#include "Components/BillboardComponent.h"
//#include "Engine/CollisionProfile.h"
//#include "Serialization/MemoryReader.h"
//#include "UObject/UObjectHash.h"
//#include "UObject/UObjectIterator.h"
//#include "Engine/Texture2D.h"
//#include "SceneManagement.h"
//#include "Engine/ReflectionCapture.h"
//#include "DerivedDataCacheInterface.h"
//#include "EngineModule.h"
//#include "ShaderCompiler.h"
//#include "UObject/RenderingObjectVersion.h"
//#include "Engine/SphereReflectionCapture.h"
//#include "Components/SphereReflectionCaptureComponent.h"
//#include "Components/DrawSphereComponent.h"
//#include "Components/BoxReflectionCaptureComponent.h"
//#include "Engine/PlaneReflectionCapture.h"
//#include "Engine/BoxReflectionCapture.h"
//#include "Components/PlaneReflectionCaptureComponent.h"
//#include "Components/BoxComponent.h"
//#include "Components/SkyLightComponent.h"
//#include "ProfilingDebugging/CookStats.h"
//#include "Engine/MapBuildDataRegistry.h"
//#include "ComponentRecreateRenderStateContext.h"

/*

ABoxReflectionCapture::ABoxReflectionCapture(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UBoxReflectionCaptureComponent>(TEXT("NewReflectionComponent")))
{
	UBoxReflectionCaptureComponent* BoxComponent = CastChecked<UBoxReflectionCaptureComponent>(GetCaptureComponent());
	BoxComponent->RelativeScale3D = FVector(1000, 1000, 400);
	RootComponent = BoxComponent;
#if WITH_EDITORONLY_DATA
	if (GetSpriteComponent())
	{
		GetSpriteComponent()->SetupAttachment(BoxComponent);
	}
	if (GetCaptureOffsetComponent())
	{
		GetCaptureOffsetComponent()->SetupAttachment(BoxComponent);
	}
#endif	//WITH_EDITORONLY_DATA 
	UBoxComponent* DrawInfluenceBox = CreateDefaultSubobject<UBoxComponent>(TEXT("DrawBox0"));
	DrawInfluenceBox->SetupAttachment(GetCaptureComponent());
	DrawInfluenceBox->bDrawOnlyIfSelected = true;
	DrawInfluenceBox->bUseEditorCompositing = true;
	DrawInfluenceBox->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	DrawInfluenceBox->InitBoxExtent(FVector(1, 1, 1));
	BoxComponent->PreviewInfluenceBox = DrawInfluenceBox;

	UBoxComponent* DrawCaptureBox = CreateDefaultSubobject<UBoxComponent>(TEXT("DrawBox1"));
	DrawCaptureBox->SetupAttachment(GetCaptureComponent());
	DrawCaptureBox->bDrawOnlyIfSelected = true;
	DrawCaptureBox->bUseEditorCompositing = true;
	DrawCaptureBox->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	DrawCaptureBox->ShapeColor = FColor(100, 90, 40);
	DrawCaptureBox->InitBoxExtent(FVector(1, 1, 1));
	BoxComponent->PreviewCaptureBox = DrawCaptureBox;
}
*/


URemotePlayReflectionCaptureComponent::URemotePlayReflectionCaptureComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	BoxTransitionDistance = 100;
}

void URemotePlayReflectionCaptureComponent::UpdatePreviewShape()
{
	if (PreviewCaptureBox)
	{
		PreviewCaptureBox->InitBoxExtent(((GetComponentTransform().GetScale3D() - FVector(BoxTransitionDistance)) / GetComponentTransform().GetScale3D()).ComponentMax(FVector::ZeroVector));
	}

	Super::UpdatePreviewShape();
}

float URemotePlayReflectionCaptureComponent::GetInfluenceBoundingRadius() const
{
	return (GetComponentTransform().GetScale3D() + FVector(BoxTransitionDistance)).Size();
}
