// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Engine/ReflectionCapture.h"
#include "RemotePlayCapture.generated.h"

class UDrawSphereComponent;

/**
 * 
 */
UCLASS(hidecategories = (Collision, Attachment, Actor))
class  ARemotePlayCapture : public AReflectionCapture
{
	GENERATED_UCLASS_BODY()
	
private:
	/** Sphere component used to visualize the capture radius */
	UPROPERTY()
		UDrawSphereComponent* DrawCaptureRadius;

public:

#if WITH_EDITOR
	//~ Begin AActor Interface.
	virtual void EditorApplyScale(const FVector& DeltaScale, const FVector* PivotLocation, bool bAltDown, bool bShiftDown, bool bCtrlDown) override;

	virtual void PostEditMove(bool bFinished) override;
	//~ End AActor Interface.
#endif

	/** Returns DrawCaptureRadius subobject **/
	 UDrawSphereComponent* GetDrawCaptureRadius() const { return DrawCaptureRadius; }
};



