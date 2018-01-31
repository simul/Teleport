// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/DefaultPawn.h"
#include "RemotePlayPawnMovementComponent.h"
#include "RemotePlayPawnBase.generated.h"

UCLASS(config=Game, Blueprintable, BlueprintType)
class REMOTEPLAY_API ARemotePlayPawnBase : public APawn
{
	GENERATED_UCLASS_BODY()

	// Begin Pawn overrides
	virtual void SetupPlayerInputComponent(UInputComponent* InInputComponent) override;
	// End Pawn overrides
	/** Returns MeshComponent subobject **/
	UStaticMeshComponent* GetMeshComponent() const { return MeshComponent; }

private:
	/** The mesh associated with this Pawn. */
	UPROPERTY(Category = Pawn, VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	UStaticMeshComponent* MeshComponent;
	
	//UPROPERTY(Category = Pawn, VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	USceneComponent *RootComponent;
protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	void InitializeCubemapCapture();
public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	/** Movement component used for movement logic in various movement modes (walking, falling, etc), containing relevant settings and functions to control movement. */
	UPROPERTY(Category = RemotePlay, VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	class URemotePlayPawnMovementComponent* RemotePlayMovement;

	/** Movement component used for movement logic in various movement modes (walking, falling, etc), containing relevant settings and functions to control movement. */
	UPROPERTY(Category = RemotePlay, VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	class USceneCaptureComponentCube* SceneCaptureCube;

	//~ Begin APawn Interface.
	virtual void PostInitializeComponents() override;
	virtual UPawnMovementComponent* GetMovementComponent() const override;
	virtual void TurnOff() override;
	virtual void Restart() override;
	virtual void PawnClientRestart() override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void UnPossessed() override;
	virtual void DisplayDebug(class UCanvas* Canvas, const FDebugDisplayInfo& DebugDisplay, float& YL, float& YPos) override;
	//~ End APawn Interface
	
};
