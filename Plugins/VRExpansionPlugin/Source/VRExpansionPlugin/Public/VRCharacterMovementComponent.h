// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "AI/Navigation/NavigationAvoidanceTypes.h"
#include "AI/RVOAvoidanceInterface.h"
#include "AITypes.h"
#include "VRRootComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "AI/Navigation/NavigationTypes.h"
#include "AI/Navigation/NavigationSystem.h"
#include "Animation/AnimationAsset.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/PawnMovementComponent.h"
#include "Interfaces/NetworkPredictionInterface.h"
#include "WorldCollision.h"
#include "Runtime/Launch/Resources/Version.h"
#include "VRBaseCharacterMovementComponent.h"
#include "VRCharacterMovementComponent.generated.h"

class FDebugDisplayInfo;
class ACharacter;
class AVRCharacter;

/** Shared pointer for easy memory management of FSavedMove_Character, for accumulating and replaying network moves. */
//typedef TSharedPtr<class FSavedMove_Character> FSavedMovePtr;


//=============================================================================
/**
 * VRCharacterMovementComponent handles movement logic for the associated Character owner.
 * It supports various movement modes including: walking, falling, swimming, flying, custom.
 *
 * Movement is affected primarily by current Velocity and Acceleration. Acceleration is updated each frame
 * based on the input vector accumulated thus far (see UPawnMovementComponent::GetPendingInputVector()).
 *
 * Networking is fully implemented, with server-client correction and prediction included.
 *
 * @see ACharacter, UPawnMovementComponent
 * @see https://docs.unrealengine.com/latest/INT/Gameplay/Framework/Pawn/Character/
 */

//DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FAIMoveCompletedSignature, FAIRequestID, RequestID, EPathFollowingResult::Type, Result);

// Using this fixes the problem where the character capsule isn't reset after a scoped movement update revert (pretty much just in StepUp operations)
class VREXPANSIONPLUGIN_API FVRCharacterScopedMovementUpdate : public FScopedMovementUpdate
{
public:

	FVRCharacterScopedMovementUpdate(USceneComponent* Component, EScopedUpdate::Type ScopeBehavior = EScopedUpdate::DeferredUpdates, bool bRequireOverlapsEventFlagToQueueOverlaps = true)
		: FScopedMovementUpdate(Component, ScopeBehavior, bRequireOverlapsEventFlagToQueueOverlaps)
	{
		UVRRootComponent* RootComponent = Cast<UVRRootComponent>(Owner);
		if (RootComponent)
		{
			InitialVRTransform = RootComponent->OffsetComponentToWorld;
		}
	}

	FTransform InitialVRTransform;

	/** Revert movement to the initial location of the Component at the start of the scoped update. Also clears pending overlaps and sets bHasMoved to false. */
	void RevertMove()
	{
		UVRRootComponent* RootComponent = Cast<UVRRootComponent>(Owner);
		if (RootComponent)
		{
			// If the base class is going to miss bad overlaps
			if (!IsTransformDirty() && !InitialVRTransform.Equals(RootComponent->OffsetComponentToWorld))
			{
				RootComponent->UpdateOverlaps();
			}

			FScopedMovementUpdate::RevertMove();
			RootComponent->GenerateOffsetToWorld();
		}
	}
};


UCLASS()
class VREXPANSIONPLUGIN_API UVRCharacterMovementComponent : public UVRBaseCharacterMovementComponent
{
	GENERATED_BODY()
public:

	UPROPERTY(BlueprintReadOnly, Transient, Category = VRMovement)
	UVRRootComponent * VRRootCapsule;

	/** Reject sweep impacts that are this close to the edge of the vertical portion of the capsule when performing vertical sweeps, and try again with a smaller capsule. */
	static const float CLIMB_SWEEP_EDGE_REJECT_DISTANCE;
	virtual bool IsWithinClimbingEdgeTolerance(const FVector& CapsuleLocation, const FVector& TestImpactPoint, const float CapsuleRadius) const;
	virtual bool VRClimbStepUp(const FVector& GravDir, const FVector& Delta, const FHitResult &InHit, FStepDownResult* OutStepDownResult = nullptr) override;

	virtual bool IsWithinEdgeTolerance(const FVector& CapsuleLocation, const FVector& TestImpactPoint, const float CapsuleRadius) const override;

	// Allow merging movement replication (may cause issues when >10 players due to capsule location
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRCharacterMovementComponent")
	bool bAllowMovementMerging;

	// Higher values will cause more slide but better step up
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRCharacterMovementComponent", meta = (ClampMin = "0.01", UIMin = "0", ClampMax = "1.0", UIMax = "1"))
	float WallRepulsionMultiplier;

	/**
	* Checks if new capsule size fits (no encroachment), and call CharacterOwner->OnStartCrouch() if successful.
	* In general you should set bWantsToCrouch instead to have the crouch persist during movement, or just use the crouch functions on the owning Character.
	* @param	bClientSimulation	true when called when bIsCrouched is replicated to non owned clients, to update collision cylinder and offset.
	*/
	virtual void Crouch(bool bClientSimulation = false) override;

	/**
	* Checks if default capsule size fits (no encroachment), and trigger OnEndCrouch() on the owner if successful.
	* @param	bClientSimulation	true when called when bIsCrouched is replicated to non owned clients, to update collision cylinder and offset.
	*/
	virtual void UnCrouch(bool bClientSimulation = false) override;

	/** @return true if the character is allowed to crouch in the current state. By default it is allowed when walking or falling, if CanEverCrouch() is true. */
	//virtual bool CanCrouchInCurrentState() const;

	///////////////////////////
	// Navigation Functions
	///////////////////////////

	/** Blueprint notification that we've completed the current movement request */
	//UPROPERTY(BlueprintAssignable, meta = (DisplayName = "MoveCompleted"))
	//FAIMoveCompletedSignature ReceiveMoveCompleted;

	virtual void OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result);

	/**
	* Checks to see if the current location is not encroaching blocking geometry so the character can leave NavWalking.
	* Restores collision settings and adjusts character location to avoid getting stuck in geometry.
	* If it's not possible, MovementMode change will be delayed until character reach collision free spot.
	* @return True if movement mode was successfully changed
	*/
	virtual bool TryToLeaveNavWalking() override;
	
	
	virtual void PhysNavWalking(float deltaTime, int32 Iterations) override;
	virtual void ProcessLanded(const FHitResult& Hit, float remainingTime, int32 Iterations) override;

	void PostPhysicsTickComponent(float DeltaTime, FCharacterMovementComponentPostPhysicsTickFunction& ThisTickFunction) override;
	void SimulateMovement(float DeltaSeconds) override;
	void MoveSmooth(const FVector& InVelocity, const float DeltaSeconds, FStepDownResult* OutStepDownResult) override;
	//void PerformMovement(float DeltaSeconds) override;

	FORCEINLINE FVector GetActorFeetLocation() const { return VRRootCapsule ? (VRRootCapsule->OffsetComponentToWorld.GetLocation() - FVector(0, 0, UpdatedComponent->Bounds.BoxExtent.Z)) : UpdatedComponent ? (UpdatedComponent->GetComponentLocation() - FVector(0, 0, UpdatedComponent->Bounds.BoxExtent.Z)) : FNavigationSystem::InvalidLocation; }
	virtual FBasedPosition GetActorFeetLocationBased() const override
	{
		return FBasedPosition(NULL, GetActorFeetLocation());
	}

	///////////////////////////
	// End Navigation Functions
	///////////////////////////
	

	///////////////////////////
	// Replication Functions
	///////////////////////////

	////////////////////////////////////
	// Network RPCs for movement
	////////////////////////////////////
	/**
	* The actual RPCs are passed to ACharacter, which wrap to the _Implementation and _Validate call here, to avoid Component RPC overhead.
	* For example:
	*		Client: UCharacterMovementComponent::ServerMove(...) => Calls CharacterOwner->ServerMove(...) triggering RPC on server
	*		Server: ACharacter::ServerMove_Implementation(...) => Calls CharacterMovement->ServerMove_Implementation
	*		To override the client call to the server RPC (on CharacterOwner), override ServerMove().
	*		To override the server implementation, override ServerMove_Implementation().
	*/


	// Using my own as I don't want to cast the standard fsavedmove
	virtual void CallServerMove(const class FSavedMove_Character* NewMove, const class FSavedMove_Character* OldMove) override;

	/** Replicated function sent by client to server - contains client movement and view info. */
	//UFUNCTION(unreliable, server, WithValidation)
	virtual void ServerMoveVR(float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 CapsuleLoc, FVRConditionalMoveRep ConditionalReps, FVector_NetQuantize100 LFDiff, uint16 CapsuleYaw, uint8 CompressedMoveFlags, FVRConditionalMoveRep2 MoveReps, uint8 ClientMovementMode);
	virtual void ServerMoveVR_Implementation(float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 CapsuleLoc, FVRConditionalMoveRep ConditionalReps, FVector_NetQuantize100 LFDiff, uint16 CapsuleYaw, uint8 CompressedMoveFlags, FVRConditionalMoveRep2 MoveReps, uint8 ClientMovementMode);
	virtual bool ServerMoveVR_Validate(float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 CapsuleLoc, FVRConditionalMoveRep ConditionalReps, FVector_NetQuantize100 LFDiff, uint16 CapsuleYaw, uint8 CompressedMoveFlags, FVRConditionalMoveRep2 MoveReps, uint8 ClientMovementMode);
	
	/** Replicated function sent by client to server - contains client movement and view info. ExLight version is used if there was no requested velocity or customVRInputVector or Accell*/
	//UFUNCTION(unreliable, server, WithValidation)
	virtual void ServerMoveVRExLight(float TimeStamp, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 CapsuleLoc, FVRConditionalMoveRep ConditionalReps, FVector_NetQuantize100 LFDiff, uint16 CapsuleYaw, uint8 CompressedMoveFlags, FVRConditionalMoveRep2 MoveReps, uint8 ClientMovementMode);
	virtual void ServerMoveVRExLight_Implementation(float TimeStamp, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 CapsuleLoc, FVRConditionalMoveRep ConditionalReps, FVector_NetQuantize100 LFDiff, uint16 CapsuleYaw, uint8 CompressedMoveFlags, FVRConditionalMoveRep2 MoveReps, uint8 ClientMovementMode);
	virtual bool ServerMoveVRExLight_Validate(float TimeStamp, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 CapsuleLoc, FVRConditionalMoveRep ConditionalReps, FVector_NetQuantize100 LFDiff, uint16 CapsuleYaw, uint8 CompressedMoveFlags, FVRConditionalMoveRep2 MoveReps, uint8 ClientMovementMode);


	/** Replicated function sent by client to server - contains client movement and view info for two moves. */
	//UFUNCTION(unreliable, server, WithValidation)
	virtual void ServerMoveVRDual(float TimeStamp0, FVector_NetQuantize10 InAccel0, uint8 PendingFlags, uint32 View0, FVector_NetQuantize100 OldCapsuleLoc, FVRConditionalMoveRep OldConditionalReps, FVector_NetQuantize100 OldLFDiff, uint16 OldCapsuleYaw, float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 CapsuleLoc, FVRConditionalMoveRep ConditionalReps, FVector_NetQuantize100 LFDiff, uint16 CapsuleYaw, uint8 NewFlags, FVRConditionalMoveRep2 MoveReps, uint8 ClientMovementMode);
	virtual void ServerMoveVRDual_Implementation(float TimeStamp0, FVector_NetQuantize10 InAccel0, uint8 PendingFlags, uint32 View0, FVector_NetQuantize100 OldCapsuleLoc, FVRConditionalMoveRep OldConditionalReps, FVector_NetQuantize100 OldLFDiff, uint16 OldCapsuleYaw, float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 CapsuleLoc, FVRConditionalMoveRep ConditionalReps, FVector_NetQuantize100 LFDiff, uint16 CapsuleYaw, uint8 NewFlags, FVRConditionalMoveRep2 MoveReps, uint8 ClientMovementMode);
	virtual bool ServerMoveVRDual_Validate(float TimeStamp0, FVector_NetQuantize10 InAccel0, uint8 PendingFlags, uint32 View0, FVector_NetQuantize100 OldCapsuleLoc, FVRConditionalMoveRep OldConditionalReps, FVector_NetQuantize100 OldLFDiff, uint16 OldCapsuleYaw, float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 CapsuleLoc, FVRConditionalMoveRep ConditionalReps, FVector_NetQuantize100 LFDiff, uint16 CapsuleYaw, uint8 NewFlags, FVRConditionalMoveRep2 MoveReps, uint8 ClientMovementMode);

	/** Replicated function sent by client to server - contains client movement and view info for two moves. ExLight version is used if there was no requested velocity or customVRInputVector or Accell */
	//UFUNCTION(unreliable, server, WithValidation)
	virtual void ServerMoveVRDualExLight(float TimeStamp0, uint8 PendingFlags, uint32 View0, FVector_NetQuantize100 OldCapsuleLoc, FVRConditionalMoveRep OldConditionalReps, FVector_NetQuantize100 OldLFDiff, uint16 OldCapsuleYaw, float TimeStamp, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 CapsuleLoc, FVRConditionalMoveRep ConditionalReps, FVector_NetQuantize100 LFDiff, uint16 CapsuleYaw, uint8 NewFlags, FVRConditionalMoveRep2 MoveReps, uint8 ClientMovementMode);
	virtual void ServerMoveVRDualExLight_Implementation(float TimeStamp0, uint8 PendingFlags, uint32 View0, FVector_NetQuantize100 OldCapsuleLoc, FVRConditionalMoveRep OldConditionalReps, FVector_NetQuantize100 OldLFDiff, uint16 OldCapsuleYaw, float TimeStamp, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 CapsuleLoc, FVRConditionalMoveRep ConditionalReps, FVector_NetQuantize100 LFDiff, uint16 CapsuleYaw, uint8 NewFlags, FVRConditionalMoveRep2 MoveReps, uint8 ClientMovementMode);
	virtual bool ServerMoveVRDualExLight_Validate(float TimeStamp0, uint8 PendingFlags, uint32 View0, FVector_NetQuantize100 OldCapsuleLoc, FVRConditionalMoveRep OldConditionalReps, FVector_NetQuantize100 OldLFDiff, uint16 OldCapsuleYaw, float TimeStamp, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 CapsuleLoc, FVRConditionalMoveRep ConditionalReps, FVector_NetQuantize100 LFDiff, uint16 CapsuleYaw, uint8 NewFlags, FVRConditionalMoveRep2 MoveReps, uint8 ClientMovementMode);


	/** Replicated function sent by client to server - contains client movement and view info for two moves. First move is non root motion, second is root motion. */
	//UFUNCTION(unreliable, server, WithValidation)
	virtual void ServerMoveVRDualHybridRootMotion(float TimeStamp0, FVector_NetQuantize10 InAccel0, uint8 PendingFlags, uint32 View0, FVector_NetQuantize100 OldCapsuleLoc, FVRConditionalMoveRep OldConditionalReps, FVector_NetQuantize100 OldLFDiff, uint16 OldCapsuleYaw, float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 CapsuleLoc, FVRConditionalMoveRep ConditionalReps, FVector_NetQuantize100 LFDiff, uint16 CapsuleYaw, uint8 NewFlags, FVRConditionalMoveRep2 MoveReps, uint8 ClientMovementMode);
	virtual void ServerMoveVRDualHybridRootMotion_Implementation(float TimeStamp0, FVector_NetQuantize10 InAccel0, uint8 PendingFlags, uint32 View0, FVector_NetQuantize100 OldCapsuleLoc, FVRConditionalMoveRep OldConditionalReps, FVector_NetQuantize100 OldLFDiff, uint16 OldCapsuleYaw, float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 CapsuleLoc, FVRConditionalMoveRep ConditionalReps, FVector_NetQuantize100 LFDiff, uint16 CapsuleYaw, uint8 NewFlags, FVRConditionalMoveRep2 MoveReps, uint8 ClientMovementMode);
	virtual bool ServerMoveVRDualHybridRootMotion_Validate(float TimeStamp0, FVector_NetQuantize10 InAccel0, uint8 PendingFlags, uint32 View0, FVector_NetQuantize100 OldCapsuleLoc, FVRConditionalMoveRep OldConditionalReps, FVector_NetQuantize100 OldLFDiff, uint16 OldCapsuleYaw, float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 CapsuleLoc, FVRConditionalMoveRep ConditionalReps, FVector_NetQuantize100 LFDiff, uint16 CapsuleYaw, uint8 NewFlags, FVRConditionalMoveRep2 MoveReps, uint8 ClientMovementMode);

	FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	FNetworkPredictionData_Server* GetPredictionData_Server() const override;

	///////////////////////////
	// End Replication Functions
	///////////////////////////

	/**
	 * Default UObject constructor.
	 */
	UVRCharacterMovementComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	float ImmersionDepth() const override;
	bool CanCrouch();

	// Don't really need to override this at all, it doesn't work that well even when fixed in VR
	//void VisualizeMovement() const override;


	/*
	bool HasRootMotion() const
	{
		return RootMotionParams.bHasRootMotion;
	}*/

	// Had to modify this, since every frame can start in penetration (capsule component moves into wall before movement tick)
	// It was throwing out the initial hit and not calling "step up", now I am only checking for penetration after adjustment but keeping the initial hit for step up.
	// this makes for FAR more responsive step ups.
	// I WILL need to override these for flying / swimming as well
	bool SafeMoveUpdatedComponent(const FVector& Delta, const FQuat& NewRotation, bool bSweep, FHitResult& OutHit, ETeleportType Teleport = ETeleportType::None);
	bool SafeMoveUpdatedComponent(const FVector& Delta, const FRotator& NewRotation, bool bSweep, FHitResult& OutHit, ETeleportType Teleport = ETeleportType::None);
	
	// This is here to force it to call the correct SafeMoveUpdatedComponent functions for floor movement
	virtual void MoveAlongFloor(const FVector& InVelocity, float DeltaSeconds, FStepDownResult* OutStepDownResult) override;

	// Modify for correct location
	virtual void ApplyRepulsionForce(float DeltaSeconds) override;

	// Update BaseOffset to be zero
	virtual void UpdateBasedMovement(float DeltaSeconds) override;

	// Stop subtracting the capsules half height
	virtual FVector GetImpartedMovementBaseVelocity() const override;

	// Cheating at the relative collision detection
	void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction);

	// Need to fill our capsule component variable here and override the default tick ordering
	virtual void SetUpdatedComponent(USceneComponent* NewUpdatedComponent) override;

	// Correct an offset sweep test
	virtual void ReplicateMoveToServer(float DeltaTime, const FVector& NewAcceleration) override;

	// Always called with the capsulecomponent location, no idea why it doesn't just get it inside it already
	// Had to force it within the function to use VRLocation instead.
	virtual void FindFloor(const FVector& CapsuleLocation, FFindFloorResult& OutFloorResult, bool bZeroDelta, const FHitResult* DownwardSweepResult) const override;

	// Need to use actual capsule location for step up
	bool StepUp(const FVector& GravDir, const FVector& Delta, const FHitResult &InHit, FStepDownResult* OutStepDownResult) override;

	virtual FVector GetPenetrationAdjustment(const FHitResult& Hit) const override
	{
		// This checks for a walking collision override on the penetrated object
		// If found then it stops penetration adjustments.
		if (MovementMode == EMovementMode::MOVE_Walking && VRRootCapsule && VRRootCapsule->bUseWalkingCollisionOverride && Hit.Component.IsValid())
		{
			ECollisionResponse WalkingResponse;
			WalkingResponse = Hit.Component->GetCollisionResponseToChannel(VRRootCapsule->WalkingCollisionOverride);

			if (WalkingResponse == ECR_Ignore || WalkingResponse == ECR_Overlap)
			{
				return FVector::ZeroVector;
			}
		}

		FVector Result = Super::GetPenetrationAdjustment(Hit);

		if (CharacterOwner)
		{
			const bool bIsProxy = (CharacterOwner->Role == ROLE_SimulatedProxy);
			float MaxDistance = bIsProxy ? MaxDepenetrationWithGeometryAsProxy : MaxDepenetrationWithGeometry;
			const AActor* HitActor = Hit.GetActor();
			if (Cast<APawn>(HitActor))
			{
				MaxDistance = bIsProxy ? MaxDepenetrationWithPawnAsProxy : MaxDepenetrationWithPawn;
			}

			Result = Result.GetClampedToMaxSize(MaxDistance);
		}

		return Result;
	}
	// MOVED THIS TO THE BASE VR CHARACTER MOVEMENT COMPONENT
	// Also added a control variable for it there
	// Skip physics channels when looking for floor
	/*virtual bool FloorSweepTest(
		FHitResult& OutHit,
		const FVector& Start,
		const FVector& End,
		ECollisionChannel TraceChannel,
		const struct FCollisionShape& CollisionShape,
		const struct FCollisionQueryParams& Params,
		const struct FCollisionResponseParams& ResponseParam
		) const override;*/

	// Multiple changes to support relative motion and ledge sweeps
	virtual void PhysWalking(float deltaTime, int32 Iterations) override;

	// Supporting the direct move injection
	virtual void PhysFlying(float deltaTime, int32 Iterations) override;
	
	// Need to use VR location, was defaulting to actor
	virtual bool ShouldCheckForValidLandingSpot(float DeltaTime, const FVector& Delta, const FHitResult& Hit) const override;

	// Overriding the physfalling because valid landing spots were computed incorrectly.
	virtual void PhysFalling(float deltaTime, int32 Iterations) override;

	// Just calls find floor
	/** Verify that the supplied hit result is a valid landing spot when falling. */
	//virtual bool IsValidLandingSpot(const FVector& CapsuleLocation, const FHitResult& Hit) const override;

	// Making sure that impulses are correct
	virtual void CapsuleTouched(UPrimitiveComponent* OverlappedComp, AActor* Other, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult) override;
};


class VREXPANSIONPLUGIN_API FSavedMove_VRCharacter : public FSavedMove_VRBaseCharacter
{

public:

	virtual void SetInitialPosition(ACharacter* C);
	virtual void PrepMoveFor(ACharacter* Character) override;

	FSavedMove_VRCharacter() : FSavedMove_VRBaseCharacter()
	{}

};

// Need this for capsule location replication
class VREXPANSIONPLUGIN_API FNetworkPredictionData_Client_VRCharacter : public FNetworkPredictionData_Client_Character
{
public:
	FNetworkPredictionData_Client_VRCharacter(const UCharacterMovementComponent& ClientMovement)
		: FNetworkPredictionData_Client_Character(ClientMovement)
	{

	}

	FSavedMovePtr AllocateNewMove()
	{
		return FSavedMovePtr(new FSavedMove_VRCharacter());
	}
};


// Need this for capsule location replication?????
class VREXPANSIONPLUGIN_API FNetworkPredictionData_Server_VRCharacter : public FNetworkPredictionData_Server_Character
{
public:
	FNetworkPredictionData_Server_VRCharacter(const UCharacterMovementComponent& ClientMovement)
		: FNetworkPredictionData_Server_Character(ClientMovement)
	{

	}

	FSavedMovePtr AllocateNewMove()
	{
		return FSavedMovePtr(new FSavedMove_VRCharacter());
	}
};