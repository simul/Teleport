// Fill out your copyright notice in the Description page of Project Settings.

#include "RemotePlayCapture.h"
#include "Components/SphereReflectionCaptureComponent.h"
#include "Components/DrawSphereComponent.h"
#include "Components/BillboardComponent.h"
#include "Engine/CollisionProfile.h"


ARemotePlayCapture::ARemotePlayCapture(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<USphereReflectionCaptureComponent>(TEXT("NewReflectionComponent")))
{
	USphereReflectionCaptureComponent* SphereComponent = CastChecked<USphereReflectionCaptureComponent>(GetCaptureComponent());
	RootComponent = SphereComponent;
#if WITH_EDITORONLY_DATA
	if (GetSpriteComponent())
	{
		GetSpriteComponent()->SetupAttachment(SphereComponent);
	}
	if (GetCaptureOffsetComponent())
	{
		GetCaptureOffsetComponent()->SetupAttachment(SphereComponent);
	}
#endif	//WITH_EDITORONLY_DATA

	UDrawSphereComponent* DrawInfluenceRadius = CreateDefaultSubobject<UDrawSphereComponent>(TEXT("DrawRadius0"));
	DrawInfluenceRadius->SetupAttachment(GetCaptureComponent());
	DrawInfluenceRadius->bDrawOnlyIfSelected = true;
	DrawInfluenceRadius->bUseEditorCompositing = true;
	DrawInfluenceRadius->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	SphereComponent->PreviewInfluenceRadius = DrawInfluenceRadius;

	DrawCaptureRadius = CreateDefaultSubobject<UDrawSphereComponent>(TEXT("DrawRadius1"));
	DrawCaptureRadius->SetupAttachment(GetCaptureComponent());
	DrawCaptureRadius->bDrawOnlyIfSelected = true;
	DrawCaptureRadius->bUseEditorCompositing = true;
	DrawCaptureRadius->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	DrawCaptureRadius->ShapeColor = FColor(100, 90, 40);
}

#if WITH_EDITOR
void ARemotePlayCapture::EditorApplyScale(const FVector& DeltaScale, const FVector* PivotLocation, bool bAltDown, bool bShiftDown, bool bCtrlDown)
{
	USphereReflectionCaptureComponent* SphereComponent = Cast<USphereReflectionCaptureComponent>(GetCaptureComponent());
	check(SphereComponent);
	const FVector ModifiedScale = DeltaScale * ( AActor::bUsePercentageBasedScaling ? 5000.0f : 50.0f );
	FMath::ApplyScaleToFloat(SphereComponent->InfluenceRadius, ModifiedScale);
	GetCaptureComponent()->SetCaptureIsDirty();
	PostEditChange();
}

void ARemotePlayCapture::PostEditMove(bool bFinished)
{
	AActor::PostEditMove(bFinished);

	GetCaptureComponent()->SetCaptureIsDirty();
}
#endif