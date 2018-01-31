// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "VRCharacter.h"
#include "VRPathFollowingComponent.h"
//#include "Runtime/Engine/Private/EnginePrivate.h"

AVRCharacter::AVRCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UVRRootComponent>(ACharacter::CapsuleComponentName).SetDefaultSubobjectClass<UVRCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	VRRootReference = NULL;
	if (GetCapsuleComponent())
	{
		VRRootReference = Cast<UVRRootComponent>(GetCapsuleComponent());
		VRRootReference->SetCapsuleSize(20.0f, 96.0f);
		VRRootReference->VRCapsuleOffset = FVector(-8.0f, 0.0f, 0.0f);
		VRRootReference->SetCollisionResponseToAllChannels(ECollisionResponse::ECR_Ignore);
		VRRootReference->SetCollisionResponseToChannel(ECollisionChannel::ECC_WorldStatic, ECollisionResponse::ECR_Block);
	}

	VRMovementReference = NULL;
	if (GetMovementComponent())
	{
		VRMovementReference = Cast<UVRBaseCharacterMovementComponent>(GetMovementComponent());
		//AddTickPrerequisiteComponent(this->GetCharacterMovement());
	}
}


FVector AVRCharacter::GetTeleportLocation(FVector OriginalLocation)
{
	FVector modifier = VRRootReference->OffsetComponentToWorld.GetLocation() - this->GetActorLocation();
	modifier.Z = 0.0f; // Null out Z
	return OriginalLocation - modifier;
}

bool AVRCharacter::TeleportTo(const FVector& DestLocation, const FRotator& DestRotation, bool bIsATest, bool bNoCheck)
{
	bool bTeleportSucceeded = Super::TeleportTo(DestLocation, DestRotation, bIsATest, bNoCheck);

	if (bTeleportSucceeded)
	{
		NotifyOfTeleport();
	}

	return bTeleportSucceeded;
}

void AVRCharacter::NotifyOfTeleport_Implementation()
{
	// Regenerate the capsule offset location - Should be done anyway in the move_impl function, but playing it safe
	if (VRRootReference)
		VRRootReference->GenerateOffsetToWorld();

	if (LeftMotionController)
		LeftMotionController->PostTeleportMoveGrippedObjects();

	if (RightMotionController)
		RightMotionController->PostTeleportMoveGrippedObjects();
}

FVector AVRCharacter::GetNavAgentLocation() const
{
	FVector AgentLocation = FNavigationSystem::InvalidLocation;

	if (GetCharacterMovement() != nullptr)
	{
		if (UVRCharacterMovementComponent * VRMove = Cast<UVRCharacterMovementComponent>(GetCharacterMovement()))
		{
			AgentLocation = VRMove->GetActorFeetLocation();
		}
		else
			AgentLocation = GetCharacterMovement()->GetActorFeetLocation();
	}

	if (FNavigationSystem::IsValidLocation(AgentLocation) == false && GetCapsuleComponent() != nullptr)
	{
		if (VRRootReference)
		{
			AgentLocation = VRRootReference->OffsetComponentToWorld.GetLocation() - FVector(0, 0, VRRootReference->GetScaledCapsuleHalfHeight());
		}
		else
			AgentLocation = GetActorLocation() - FVector(0, 0, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	}

	return AgentLocation;
}

void AVRCharacter::ExtendedSimpleMoveToLocation(const FVector& GoalLocation, float AcceptanceRadius, bool bStopOnOverlap, bool bUsePathfinding, bool bProjectDestinationToNavigation, bool bCanStrafe, TSubclassOf<UNavigationQueryFilter> FilterClass, bool bAllowPartialPaths)
{
	UNavigationSystem* NavSys = Controller ? UNavigationSystem::GetCurrent(Controller->GetWorld()) : nullptr;
	if (NavSys == nullptr || Controller == nullptr )
	{
		UE_LOG(LogTemp, Warning, TEXT("UVRCharacter::ExtendedSimpleMoveToLocation called for NavSys:%s Controller:%s (if any of these is None then there's your problem"),
			*GetNameSafe(NavSys), *GetNameSafe(Controller));
		return;
	}

	UPathFollowingComponent* PFollowComp = nullptr;
	Controller->InitNavigationControl(PFollowComp);

	if (PFollowComp == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("ExtendedSimpleMoveToLocation - No PathFollowingComponent Found"));
		return;
	}

	if (!PFollowComp->IsPathFollowingAllowed())
	{
		UE_LOG(LogTemp, Warning, TEXT("ExtendedSimpleMoveToLocation - Path Following Movement Is Not Set To Allowed"));
		return;
	}

	EPathFollowingReachMode ReachMode;
	if (bStopOnOverlap)
		ReachMode = EPathFollowingReachMode::OverlapAgent;
	else
		ReachMode = EPathFollowingReachMode::ExactLocation;

	bool bAlreadyAtGoal = false;

	if(UVRPathFollowingComponent * pathcomp = Cast<UVRPathFollowingComponent>(PFollowComp))
		bAlreadyAtGoal = pathcomp->HasReached(GoalLocation, /*EPathFollowingReachMode::OverlapAgent*/ReachMode);
	else
		bAlreadyAtGoal = PFollowComp->HasReached(GoalLocation, /*EPathFollowingReachMode::OverlapAgent*/ReachMode);

	// script source, keep only one move request at time
	if (PFollowComp->GetStatus() != EPathFollowingStatus::Idle)
	{
		if (GetNetMode() == ENetMode::NM_Client)
		{
			// Stop the movement here, not keeping the velocity because it bugs out for clients, might be able to fix.
			PFollowComp->AbortMove(*NavSys, FPathFollowingResultFlags::ForcedScript | FPathFollowingResultFlags::NewRequest
				, FAIRequestID::AnyRequest, /*bAlreadyAtGoal ? */EPathFollowingVelocityMode::Reset /*: EPathFollowingVelocityMode::Keep*/);
		}
		else
		{
			PFollowComp->AbortMove(*NavSys, FPathFollowingResultFlags::ForcedScript | FPathFollowingResultFlags::NewRequest
				, FAIRequestID::AnyRequest, bAlreadyAtGoal ? EPathFollowingVelocityMode::Reset : EPathFollowingVelocityMode::Keep);
		}
	}

	if (bAlreadyAtGoal)
	{
		PFollowComp->RequestMoveWithImmediateFinish(EPathFollowingResult::Success);
	}
	else
	{
		const ANavigationData* NavData = NavSys->GetNavDataForProps(Controller->GetNavAgentPropertiesRef());
		if (NavData)
		{
			FPathFindingQuery Query(Controller, *NavData, Controller->GetNavAgentLocation(), GoalLocation);
			FPathFindingResult Result = NavSys->FindPathSync(Query);
			if (Result.IsSuccessful())
			{
				FAIMoveRequest MoveReq(GoalLocation);
				MoveReq.SetUsePathfinding(bUsePathfinding);
				MoveReq.SetAllowPartialPath(bAllowPartialPaths);
				MoveReq.SetProjectGoalLocation(bProjectDestinationToNavigation);
				MoveReq.SetNavigationFilter(*FilterClass ? FilterClass : DefaultNavigationFilterClass);
				MoveReq.SetAcceptanceRadius(AcceptanceRadius);
				MoveReq.SetReachTestIncludesAgentRadius(bStopOnOverlap);
				MoveReq.SetCanStrafe(bCanStrafe);
				MoveReq.SetReachTestIncludesGoalRadius(true);

				PFollowComp->RequestMove(/*FAIMoveRequest(GoalLocation)*/MoveReq, Result.Path);
			}
			else if (PFollowComp->GetStatus() != EPathFollowingStatus::Idle)
			{
				PFollowComp->RequestMoveWithImmediateFinish(EPathFollowingResult::Invalid);
			}
		}
	}
}

bool AVRCharacter::ServerMoveVR_Validate(float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 CapsuleLoc, FVRConditionalMoveRep ConditionalReps, FVector_NetQuantize100 LFDiff, uint16 CapsuleYaw, uint8 MoveFlags, FVRConditionalMoveRep2 MoveReps, uint8 ClientMovementMode)
{
	return ((UVRCharacterMovementComponent*)GetCharacterMovement())->ServerMoveVR_Validate(TimeStamp, InAccel, ClientLoc, CapsuleLoc, ConditionalReps, LFDiff, CapsuleYaw, MoveFlags, MoveReps, ClientMovementMode);
}

bool AVRCharacter::ServerMoveVRExLight_Validate(float TimeStamp, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 CapsuleLoc, FVRConditionalMoveRep ConditionalReps, FVector_NetQuantize100 LFDiff, uint16 CapsuleYaw, uint8 MoveFlags, FVRConditionalMoveRep2 MoveReps, uint8 ClientMovementMode)
{
	return ((UVRCharacterMovementComponent*)GetCharacterMovement())->ServerMoveVRExLight_Validate(TimeStamp, ClientLoc, CapsuleLoc, ConditionalReps, LFDiff, CapsuleYaw, MoveFlags, MoveReps, ClientMovementMode);
}

bool AVRCharacter::ServerMoveVRDual_Validate(float TimeStamp0, FVector_NetQuantize10 InAccel0, uint8 PendingFlags,uint32 View0, FVector_NetQuantize100 OldCapsuleLoc, FVRConditionalMoveRep OldConditionalReps, FVector_NetQuantize100 OldLFDiff, uint16 OldCapsuleYaw, float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 CapsuleLoc, FVRConditionalMoveRep ConditionalReps, FVector_NetQuantize100 LFDiff, uint16 CapsuleYaw, uint8 NewFlags, FVRConditionalMoveRep2 MoveReps, uint8 ClientMovementMode)
{
	return ((UVRCharacterMovementComponent*)GetCharacterMovement())->ServerMoveVRDual_Validate(TimeStamp0, InAccel0, PendingFlags, View0, OldCapsuleLoc, OldConditionalReps, OldLFDiff, OldCapsuleYaw, TimeStamp, InAccel, ClientLoc, CapsuleLoc, ConditionalReps, LFDiff, CapsuleYaw, NewFlags, MoveReps, ClientMovementMode);
}

bool AVRCharacter::ServerMoveVRDualExLight_Validate(float TimeStamp0, uint8 PendingFlags,uint32 View0, FVector_NetQuantize100 OldCapsuleLoc, FVRConditionalMoveRep OldConditionalReps, FVector_NetQuantize100 OldLFDiff, uint16 OldCapsuleYaw, float TimeStamp, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 CapsuleLoc, FVRConditionalMoveRep ConditionalReps, FVector_NetQuantize100 LFDiff, uint16 CapsuleYaw, uint8 NewFlags, FVRConditionalMoveRep2 MoveReps, uint8 ClientMovementMode)
{
	return ((UVRCharacterMovementComponent*)GetCharacterMovement())->ServerMoveVRDualExLight_Validate(TimeStamp0, PendingFlags, View0, OldCapsuleLoc, OldConditionalReps, OldLFDiff, OldCapsuleYaw, TimeStamp, ClientLoc, CapsuleLoc, ConditionalReps, LFDiff, CapsuleYaw, NewFlags, MoveReps, ClientMovementMode);
}

bool AVRCharacter::ServerMoveVRDualHybridRootMotion_Validate(float TimeStamp0, FVector_NetQuantize10 InAccel0, uint8 PendingFlags,uint32 View0, FVector_NetQuantize100 OldCapsuleLoc, FVRConditionalMoveRep OldConditionalReps, FVector_NetQuantize100 OldLFDiff, uint16 OldCapsuleYaw, float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 CapsuleLoc, FVRConditionalMoveRep ConditionalReps, FVector_NetQuantize100 LFDiff, uint16 CapsuleYaw, uint8 NewFlags, FVRConditionalMoveRep2 MoveReps, uint8 ClientMovementMode)
{
	return ((UVRCharacterMovementComponent*)GetCharacterMovement())->ServerMoveVRDualHybridRootMotion_Validate(TimeStamp0, InAccel0, PendingFlags, View0, OldCapsuleLoc, OldConditionalReps, OldLFDiff, OldCapsuleYaw, TimeStamp, InAccel, ClientLoc, CapsuleLoc, ConditionalReps, LFDiff, CapsuleYaw, NewFlags, MoveReps, ClientMovementMode);
}

void AVRCharacter::ServerMoveVRDualHybridRootMotion_Implementation(
	float TimeStamp0,
	FVector_NetQuantize10 InAccel0,
	uint8 PendingFlags,
	uint32 View0,
	FVector_NetQuantize100 OldCapsuleLoc,
	FVRConditionalMoveRep OldConditionalReps,
	FVector_NetQuantize100 OldLFDiff,
	uint16 OldCapsuleYaw,
	float TimeStamp,
	FVector_NetQuantize10 InAccel,
	FVector_NetQuantize100 ClientLoc,
	FVector_NetQuantize100 CapsuleLoc,
	FVRConditionalMoveRep ConditionalReps,
	FVector_NetQuantize100 LFDiff,
	uint16 CapsuleYaw,
	uint8 NewFlags,
	FVRConditionalMoveRep2 MoveReps,
	uint8 ClientMovementMode)
{
	((UVRCharacterMovementComponent*)GetCharacterMovement())->ServerMoveVRDualHybridRootMotion_Implementation(
		TimeStamp0,
		InAccel0,
		PendingFlags,
		View0,
		OldCapsuleLoc,
		OldConditionalReps,
		OldLFDiff,
		OldCapsuleYaw,
		TimeStamp,
		InAccel,
		ClientLoc,
		CapsuleLoc,
		ConditionalReps,
		LFDiff,
		CapsuleYaw,
		NewFlags,
		MoveReps,
		ClientMovementMode);
}

void AVRCharacter::ServerMoveVRDual_Implementation(
	float TimeStamp0,
	FVector_NetQuantize10 InAccel0,
	uint8 PendingFlags,
	uint32 View0,
	FVector_NetQuantize100 OldCapsuleLoc,
	FVRConditionalMoveRep OldConditionalReps,
	FVector_NetQuantize100 OldLFDiff,
	uint16 OldCapsuleYaw,
	float TimeStamp,
	FVector_NetQuantize10 InAccel,
	FVector_NetQuantize100 ClientLoc,
	FVector_NetQuantize100 CapsuleLoc,
	FVRConditionalMoveRep ConditionalReps,
	FVector_NetQuantize100 LFDiff,
	uint16 CapsuleYaw,
	uint8 NewFlags,
	FVRConditionalMoveRep2 MoveReps,
	uint8 ClientMovementMode)
{
	((UVRCharacterMovementComponent*)GetCharacterMovement())->ServerMoveVRDual_Implementation(
		TimeStamp0,
		InAccel0,
		PendingFlags,
		View0,
		OldCapsuleLoc,
		OldConditionalReps,
		OldLFDiff,
		OldCapsuleYaw,
		TimeStamp,
		InAccel,
		ClientLoc,
		CapsuleLoc,
		ConditionalReps,
		LFDiff,
		CapsuleYaw,
		NewFlags,
		MoveReps,
		ClientMovementMode);
}

void AVRCharacter::ServerMoveVRDualExLight_Implementation(
	float TimeStamp0,
	uint8 PendingFlags,
	uint32 View0,
	FVector_NetQuantize100 OldCapsuleLoc,
	FVRConditionalMoveRep OldConditionalReps,
	FVector_NetQuantize100 OldLFDiff,
	uint16 OldCapsuleYaw,
	float TimeStamp,
	FVector_NetQuantize100 ClientLoc,
	FVector_NetQuantize100 CapsuleLoc,
	FVRConditionalMoveRep ConditionalReps,
	FVector_NetQuantize100 LFDiff,
	uint16 CapsuleYaw,
	uint8 NewFlags,
	FVRConditionalMoveRep2 MoveReps,
	uint8 ClientMovementMode)
{
	((UVRCharacterMovementComponent*)GetCharacterMovement())->ServerMoveVRDualExLight_Implementation(
		TimeStamp0,
		PendingFlags,
		View0,
		OldCapsuleLoc,
		OldConditionalReps,
		OldLFDiff,
		OldCapsuleYaw,
		TimeStamp,
		ClientLoc,
		CapsuleLoc,
		ConditionalReps,
		LFDiff,
		CapsuleYaw,
		NewFlags,
		MoveReps,
		ClientMovementMode);
}

void AVRCharacter::ServerMoveVRExLight_Implementation(
	float TimeStamp,
	FVector_NetQuantize100 ClientLoc,
	FVector_NetQuantize100 CapsuleLoc,
	FVRConditionalMoveRep ConditionalReps,
	FVector_NetQuantize100 LFDiff,
	uint16 CapsuleYaw,
	uint8 MoveFlags,
	FVRConditionalMoveRep2 MoveReps,
	uint8 ClientMovementMode)
{
	((UVRCharacterMovementComponent*)GetCharacterMovement())->ServerMoveVRExLight_Implementation(
		TimeStamp,
		ClientLoc,
		CapsuleLoc,
		ConditionalReps,
		LFDiff,
		CapsuleYaw,
		MoveFlags,
		MoveReps,
		ClientMovementMode);
}

void AVRCharacter::ServerMoveVR_Implementation(
	float TimeStamp,
	FVector_NetQuantize10 InAccel,
	FVector_NetQuantize100 ClientLoc,
	FVector_NetQuantize100 CapsuleLoc,
	FVRConditionalMoveRep ConditionalReps,
	FVector_NetQuantize100 LFDiff,
	uint16 CapsuleYaw,
	uint8 MoveFlags,
	FVRConditionalMoveRep2 MoveReps,
	uint8 ClientMovementMode)
{
	((UVRCharacterMovementComponent*)GetCharacterMovement())->ServerMoveVR_Implementation(
		TimeStamp,
		InAccel,
		ClientLoc,
		CapsuleLoc,
		ConditionalReps,
		LFDiff,
		CapsuleYaw,
		MoveFlags,
		MoveReps,
		ClientMovementMode);
}
