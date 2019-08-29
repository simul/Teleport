// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Components/BoxReflectionCaptureComponent.h"
#include "RemotePlayReflectionCaptureComponent.generated.h"

UCLASS(hidecategories=(Collision, Object, Physics, SceneComponent), MinimalAPI)
class URemotePlayReflectionCaptureComponent : public UReflectionCaptureComponent
{
	GENERATED_UCLASS_BODY()

	/** Adjust capture transition distance */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=ReflectionCapture, meta=(UIMin = "1", UIMax = "1000"))
	float BoxTransitionDistance;

	UPROPERTY()
	class UBoxComponent* PreviewInfluenceBox;

	UPROPERTY()
	class UBoxComponent* PreviewCaptureBox;

public:
	virtual void UpdatePreviewShape() override;
	virtual float GetInfluenceBoundingRadius() const override;
};

