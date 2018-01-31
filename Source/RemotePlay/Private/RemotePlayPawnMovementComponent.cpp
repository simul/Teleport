// Copyright Simul

#include "RemotePlayPawnMovementComponent.h"
#include "DisplayDebugHelpers.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "GameFramework/WorldSettings.h"
#include "DrawDebugHelpers.h"
#include "RemotePlayPawnBase.h"

URemotePlayPawnMovementComponent::URemotePlayPawnMovementComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	MaxSpeed = 1200.f;
	Acceleration = 4000.f;
	Deceleration = 8000.f;
	TurningBoost = 8.0f;
	bPositionCorrected = false;

	ResetMoveState();
}

void URemotePlayPawnMovementComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	if (ShouldSkipUpdate(DeltaTime))
	{
		return;
	}

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!PawnOwner || !UpdatedComponent)
	{
		return;
	}

	const AController* Controller = PawnOwner->GetController();
	if (Controller && Controller->IsLocalController())
	{
		// apply input for local players but also for AI that's not following a navigation path at the moment
		if (Controller->IsLocalPlayerController() == true || Controller->IsFollowingAPath() == false || bUseAccelerationForPaths)
		{
			ApplyControlInputToVelocity(DeltaTime);
		}
		// if it's not player controller, but we do have a controller, then it's AI
		// (that's not following a path) and we need to limit the speed
		else if (IsExceedingMaxSpeed(MaxSpeed) == true)
		{
			Velocity = Velocity.GetUnsafeNormal() * MaxSpeed;
		}

		LimitWorldBounds();
		bPositionCorrected = false;

		// Move actor
		FVector Delta = Velocity * DeltaTime;

		if (!Delta.IsNearlyZero(1e-6f))
		{
			const FVector OldLocation = UpdatedComponent->GetComponentLocation();
			const FQuat Rotation = UpdatedComponent->GetComponentQuat();

			FHitResult Hit(1.f);
			SafeMoveUpdatedComponent(Delta, Rotation, true, Hit);

			if (Hit.IsValidBlockingHit())
			{
				HandleImpact(Hit, DeltaTime, Delta);
				// Try to slide the remaining distance along the surface.
				SlideAlongSurface(Delta, 1.f-Hit.Time, Hit.Normal, Hit, true);
			}

			// Update velocity
			// We don't want position changes to vastly reverse our direction (which can happen due to penetration fixups etc)
			if (!bPositionCorrected)
			{
				const FVector NewLocation = UpdatedComponent->GetComponentLocation();
				Velocity = ((NewLocation - OldLocation) / DeltaTime);
			}
		}

		// Finalize
		UpdateComponentVelocity();
	}
};

void URemotePlayPawnMovementComponent::PostLoad()
{
	Super::PostLoad();

	RemotePlayPawnBaseOwner = Cast<ARemotePlayPawnBase>(PawnOwner);
}

bool URemotePlayPawnMovementComponent::LimitWorldBounds()
{
	AWorldSettings* WorldSettings = PawnOwner ? PawnOwner->GetWorldSettings() : NULL;
	if (!WorldSettings || !WorldSettings->bEnableWorldBoundsChecks || !UpdatedComponent)
	{
		return false;
	}

	const FVector CurrentLocation = UpdatedComponent->GetComponentLocation();
	if ( CurrentLocation.Z < WorldSettings->KillZ )
	{
		Velocity.Z = FMath::Min(GetMaxSpeed(), WorldSettings->KillZ - CurrentLocation.Z + 2.0f);
		return true;
	}

	return false;
}

void URemotePlayPawnMovementComponent::ApplyControlInputToVelocity(float DeltaTime)
{
	const FVector ControlAcceleration = GetPendingInputVector().GetClampedToMaxSize(1.f);

	const float AnalogInputModifier = (ControlAcceleration.SizeSquared() > 0.f ? ControlAcceleration.Size() : 0.f);
	const float MaxPawnSpeed = GetMaxSpeed() * AnalogInputModifier;
	const bool bExceedingMaxSpeed = IsExceedingMaxSpeed(MaxPawnSpeed);

	if (AnalogInputModifier > 0.f && !bExceedingMaxSpeed)
	{
		// Apply change in velocity direction
		if (Velocity.SizeSquared() > 0.f)
		{
			// Change direction faster than only using acceleration, but never increase velocity magnitude.
			const float TimeScale = FMath::Clamp(DeltaTime * TurningBoost, 0.f, 1.f);
			Velocity = Velocity + (ControlAcceleration * Velocity.Size() - Velocity) * TimeScale;
		}
	}
	else
	{
		// Dampen velocity magnitude based on deceleration.
		if (Velocity.SizeSquared() > 0.f)
		{
			const FVector OldVelocity = Velocity;
			const float VelSize = FMath::Max(Velocity.Size() - FMath::Abs(Deceleration) * DeltaTime, 0.f);
			Velocity = Velocity.GetSafeNormal() * VelSize;

			// Don't allow braking to lower us below max speed if we started above it.
			if (bExceedingMaxSpeed && Velocity.SizeSquared() < FMath::Square(MaxPawnSpeed))
			{
				Velocity = OldVelocity.GetSafeNormal() * MaxPawnSpeed;
			}
		}
	}

	// Apply acceleration and clamp velocity magnitude.
	const float NewMaxSpeed = (IsExceedingMaxSpeed(MaxPawnSpeed)) ? Velocity.Size() : MaxPawnSpeed;
	Velocity += ControlAcceleration * FMath::Abs(Acceleration) * DeltaTime;
	Velocity = Velocity.GetClampedToMaxSize(NewMaxSpeed);

	ConsumeInputVector();
}

bool URemotePlayPawnMovementComponent::ResolvePenetrationImpl(const FVector& Adjustment, const FHitResult& Hit, const FQuat& NewRotationQuat)
{
	bPositionCorrected |= Super::ResolvePenetrationImpl(Adjustment, Hit, NewRotationQuat);
	return bPositionCorrected;
}

void URemotePlayPawnMovementComponent::PerformMovement(float DeltaSeconds)
{
}


void URemotePlayPawnMovementComponent::SimulatedTick(float DeltaSeconds)
{
}

/** Simulate movement on a non-owning client. Called by SimulatedTick(). */
 void URemotePlayPawnMovementComponent::SimulateMovement(float DeltaTime)
 {
 }

 void URemotePlayPawnMovementComponent::ForceReplicationUpdate()
 {
	 if (HasPredictionData_Server())
	 {
//		 GetPredictionData_Server()->LastUpdateTime = GetWorld()->TimeSeconds - 10.f;
	 }
 }

float URemotePlayPawnMovementComponent::GetNetworkSafeRandomAngleDegrees() const
{
	float Angle = FMath::SRand() * 360.f;

	return Angle;
}

FVector URemotePlayPawnMovementComponent::RoundAcceleration(FVector InAccel) const
{
	return InAccel;
}

void URemotePlayPawnMovementComponent::SendClientAdjustment() 
{
}

void URemotePlayPawnMovementComponent::ForcePositionUpdate(float DeltaTime) 
{
}

void URemotePlayPawnMovementComponent::SmoothCorrection(const FVector& OldLocation, const FQuat& OldRotation, const FVector& NewLocation, const FQuat& NewRotation)
{
}

/** Get prediction data for a client game. Should not be used if not running as a client. Allocates the data on demand and can be overridden to allocate a custom if desired. Result must be a FNetworkPredictionData_Client_RPP. */
FNetworkPredictionData_Client* URemotePlayPawnMovementComponent::GetPredictionData_Client() const
{
	if (ClientPredictionData == nullptr)
	{
		URemotePlayPawnMovementComponent* MutableThis = const_cast<URemotePlayPawnMovementComponent*>(this);
		MutableThis->ClientPredictionData = new FNetworkPredictionData_Client_RPP(*this);
	}

	return ClientPredictionData;
}
/** Get prediction data for a server game. Should not be used if not running as a server. Allocates the data on demand and can be overridden to allocate a custom if desired. Result must be a FNetworkPredictionData_Server_RPP. */
FNetworkPredictionData_Server* URemotePlayPawnMovementComponent::GetPredictionData_Server() const
{
	if (ServerPredictionData == nullptr)
	{
		URemotePlayPawnMovementComponent* MutableThis = const_cast<URemotePlayPawnMovementComponent*>(this);
		MutableThis->ServerPredictionData = new FNetworkPredictionData_Server_RPP(*this);
	}

	return ServerPredictionData;
}

FNetworkPredictionData_Client_RPP* URemotePlayPawnMovementComponent::GetPredictionData_Client_RPP() const
{
	if (ClientPredictionData == nullptr)
	{
		URemotePlayPawnMovementComponent* MutableThis = const_cast<URemotePlayPawnMovementComponent*>(this);
		MutableThis->ClientPredictionData = new FNetworkPredictionData_Client_RPP(*this);
	}

	return ClientPredictionData;
}

FNetworkPredictionData_Server_RPP* URemotePlayPawnMovementComponent::GetPredictionData_Server_RPP() const
{
	if (ServerPredictionData == nullptr)
	{
		URemotePlayPawnMovementComponent* MutableThis = const_cast<URemotePlayPawnMovementComponent*>(this);
		MutableThis->ServerPredictionData = new FNetworkPredictionData_Server_RPP(*this);
	}

	return ServerPredictionData;
}

bool URemotePlayPawnMovementComponent::HasPredictionData_Client() const
{
	return (ClientPredictionData != nullptr) && HasValidData();
}
bool URemotePlayPawnMovementComponent::HasPredictionData_Server() const
{
	return (ServerPredictionData != nullptr) && HasValidData();
}

bool URemotePlayPawnMovementComponent::HasValidData() const
{
	bool bIsValid = UpdatedComponent && IsValid(RemotePlayPawnBaseOwner);
#if ENABLE_NAN_DIAGNOSTIC
	if (bIsValid)
	{
		// NaN-checking updates
		if (Velocity.ContainsNaN())
		{
			logOrEnsureNanError(TEXT("URemotePlayPawnMovementComponent::HasValidData() detected NaN/INF for (%s) in Velocity:\n%s"), *GetPathNameSafe(this), *Velocity.ToString());
			URemotePlayPawnMovementComponent* MutableThis = const_cast<URemotePlayPawnMovementComponent*>(this);
			MutableThis->Velocity = FVector::ZeroVector;
		}
		if (!UpdatedComponent->GetComponentTransform().IsValid())
		{
			logOrEnsureNanError(TEXT("URemotePlayPawnMovementComponent::HasValidData() detected NaN/INF for (%s) in UpdatedComponent->ComponentTransform:\n%s"), *GetPathNameSafe(this), *UpdatedComponent->GetComponentTransform().ToHumanReadableString());
		}
		if (UpdatedComponent->GetComponentRotation().ContainsNaN())
		{
			logOrEnsureNanError(TEXT("URemotePlayPawnMovementComponent::HasValidData() detected NaN/INF for (%s) in UpdatedComponent->GetComponentRotation():\n%s"), *GetPathNameSafe(this), *UpdatedComponent->GetComponentRotation().ToString());
		}
	}
#endif
	return bIsValid;
}

void URemotePlayPawnMovementComponent::ResetPredictionData_Client()
{
}
void URemotePlayPawnMovementComponent::ResetPredictionData_Server()
{
}

FNetworkPredictionData_Client_RPP::FNetworkPredictionData_Client_RPP(const URemotePlayPawnMovementComponent& ClientMovement)
{
}

FNetworkPredictionData_Client_RPP::~FNetworkPredictionData_Client_RPP()
{
}

FNetworkPredictionData_Server_RPP::FNetworkPredictionData_Server_RPP(const URemotePlayPawnMovementComponent& ClientMovement)
{
}

FNetworkPredictionData_Server_RPP::~FNetworkPredictionData_Server_RPP()
{
}