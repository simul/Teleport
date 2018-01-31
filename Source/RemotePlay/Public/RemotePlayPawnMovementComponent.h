// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PawnMovementComponent.h"
#include "Interfaces/NetworkPredictionInterface.h"
#include "RemotePlayPawnMovementComponent.generated.h"

/**
 * 
 */
UCLASS(ClassGroup = Movement, meta = (BlueprintSpawnableComponent))
class REMOTEPLAY_API URemotePlayPawnMovementComponent : public UPawnMovementComponent, public INetworkPredictionInterface
{
	GENERATED_UCLASS_BODY()

	//Begin UActorComponent Interface
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
	//End UActorComponent Interface
	void PostLoad() override;

	//Begin UMovementComponent Interface
	virtual float GetMaxSpeed() const override { return MaxSpeed; }

protected:
	virtual bool ResolvePenetrationImpl(const FVector& Adjustment, const FHitResult& Hit, const FQuat& NewRotation) override;
protected:

	/** Character movement component belongs to */
	UPROPERTY(Transient, DuplicateTransient)
	class ARemotePlayPawnBase* RemotePlayPawnBaseOwner;
public:
	//End UMovementComponent Interface

	/** Maximum velocity magnitude allowed for the controlled Pawn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=FloatingPawnMovement)
		float MaxSpeed;

	/** Acceleration applied by input (rate of change of velocity) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=FloatingPawnMovement)
		float Acceleration;

	/** Deceleration applied when there is no input (rate of change of velocity) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=FloatingPawnMovement)
		float Deceleration;

	/**
	* Setting affecting extra force applied when changing direction, making turns have less drift and become more responsive.
	* Velocity magnitude is not allowed to increase, that only happens due to normal acceleration. It may decrease with large direction changes.
	* Larger values apply extra force to reach the target direction more quickly, while a zero value disables any extra turn force.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=FloatingPawnMovement, meta=(ClampMin="0", UIMin="0"))
		float TurningBoost;

	bool HasValidData() const;
protected:

	/** Update Velocity based on input. Also applies gravity. */
	virtual void ApplyControlInputToVelocity(float DeltaTime);

	/** Prevent Pawn from leaving the world bounds (if that restriction is enabled in WorldSettings) */
	virtual bool LimitWorldBounds();

	/** Set to true when a position correction is applied. Used to avoid recalculating velocity when this occurs. */
	UPROPERTY(Transient)
	uint32 bPositionCorrected:1;	
	

protected:

	// Movement functions broken out based on owner's network Role.
	// TickComponent calls the correct version based on the Role.
	// These may be called during move playback and correction during network updates.
	//

	/** Perform movement on an autonomous client */
	virtual void PerformMovement(float DeltaTime);

	/** Special Tick for Simulated Proxies */
	void SimulatedTick(float DeltaSeconds);

	/** Simulate movement on a non-owning client. Called by SimulatedTick(). */
	virtual void SimulateMovement(float DeltaTime);

public:

	/** Force a client update by making it appear on the server that the client hasn't updated in a long time. */
	virtual void ForceReplicationUpdate();
	
	/**
	 * Generate a random angle in degrees that is approximately equal between client and server.
	 * Note that in networked games this result changes with low frequency and has a low period,
	 * so should not be used for frequent randomization.
	 */
	virtual float GetNetworkSafeRandomAngleDegrees() const;

	/** Round acceleration, for better consistency and lower bandwidth in networked games. */
	virtual FVector RoundAcceleration(FVector InAccel) const;

	//--------------------------------
	// INetworkPredictionInterface implementation
	virtual void SendClientAdjustment() override;
	virtual void ForcePositionUpdate(float DeltaTime) override;
	/**
	 * React to new transform from network update. Sets bNetworkSmoothingComplete to false to ensure future smoothing updates.
	 * IMPORTANT: It is expected that this function triggers any movement/transform updates to match the network update if desired.
	 */
	virtual void SmoothCorrection(const FVector& OldLocation, const FQuat& OldRotation, const FVector& NewLocation, const FQuat& NewRotation) override;

	/** Get prediction data for a client game. Should not be used if not running as a client. Allocates the data on demand and can be overridden to allocate a custom override if desired. Result must be a FNetworkPredictionData_Client_RPP. */
	virtual class FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	/** Get prediction data for a server game. Should not be used if not running as a server. Allocates the data on demand and can be overridden to allocate a custom override if desired. Result must be a FNetworkPredictionData_Server_RPP. */
	virtual class FNetworkPredictionData_Server* GetPredictionData_Server() const override;

	class FNetworkPredictionData_Client_RPP* GetPredictionData_Client_RPP() const;
	class FNetworkPredictionData_Server_RPP* GetPredictionData_Server_RPP() const;

	virtual bool HasPredictionData_Client() const override;
	virtual bool HasPredictionData_Server() const override;

	virtual void ResetPredictionData_Client() override;
	virtual void ResetPredictionData_Server() override;

protected:
	class FNetworkPredictionData_Client_RPP* ClientPredictionData;
	class FNetworkPredictionData_Server_RPP* ServerPredictionData;
#if 0
	/**
	 * Smooth mesh location for network interpolation, based on values set up by SmoothCorrection.
	 * Internally this simply calls SmoothClientPosition_Interpolate() then SmoothClientPosition_UpdateVisuals().
	 * This function is not called when bNetworkSmoothingComplete is true.
	 * @param DeltaSeconds Time since last update.
	 */
	virtual void SmoothClientPosition(float DeltaSeconds);

	/**
	 * Update interpolation values for client smoothing. Does not change actual mesh location.
	 * Sets bNetworkSmoothingComplete to true when the interpolation reaches the target.
	 */
	void SmoothClientPosition_Interpolate(float DeltaSeconds);

	/** Update mesh location based on interpolated values. */
	void SmoothClientPosition_UpdateVisuals();

	static uint32 PackYawAndPitchTo32(const float Yaw, const float Pitch); 

	/*
	========================================================================
	Here's how player movement prediction, replication and correction works in network games:
	
	Every tick, the TickComponent() function is called.  It figures out the acceleration and rotation change for the frame,
	and then calls PerformMovement() (for locally controlled Characters), or ReplicateMoveToServer() (if it's a network client).
	
	ReplicateMoveToServer() saves the move (in the PendingMove list), calls PerformMovement(), and then replicates the move
	to the server by calling the replicated function ServerMove() - passing the movement parameters, the client's
	resultant position, and a timestamp.
	
	ServerMove() is executed on the server.  It decodes the movement parameters and causes the appropriate movement
	to occur.  It then looks at the resulting position and if enough time has passed since the last response, or the
	position error is significant enough, the server calls ClientAdjustPosition(), a replicated function.
	
	ClientAdjustPosition() is executed on the client.  The client sets its position to the servers version of position,
	and sets the bUpdatePosition flag to true.
	
	When TickComponent() is called on the client again, if bUpdatePosition is true, the client will call
	ClientUpdatePosition() before calling PerformMovement().  ClientUpdatePosition() replays all the moves in the pending
	move list which occurred after the timestamp of the move the server was adjusting.
	*/

	/** Perform local movement and send the move to the server. */
	virtual void ReplicateMoveToServer(float DeltaTime, const FVector& NewAcceleration);

	/** If bUpdatePosition is true, then replay any unacked moves. Returns whether any moves were actually replayed. */
	virtual bool ClientUpdatePositionAfterServerUpdate();

	/** Call the appropriate replicated servermove() function to send a client player move to the server. */
	virtual void CallServerMove(const class FSavedMove_RPP* NewMove, const class FSavedMove_RPP* OldMove);
	
	/**
	 * Have the server check if the client is outside an error tolerance, and queue a client adjustment if so.
	 * If either GetPredictionData_Server_RPP()->bForceClientUpdate or ServerCheckClientError() are true, the client adjustment will be sent.
	 * RelativeClientLocation will be a relative location if MovementBaseUtility::UseRelativePosition(ClientMovementBase) is true, or a world location if false.
	 * @see ServerCheckClientError()
	 */
	virtual void ServerMoveHandleClientError(float ClientTimeStamp, float DeltaTime, const FVector& Accel, const FVector& RelativeClientLocation, UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode);

	/**
	 * Check for Server-Client disagreement in position or other movement state important enough to trigger a client correction.
	 * @see ServerMoveHandleClientError()
	 */
	virtual bool ServerCheckClientError(float ClientTimeStamp, float DeltaTime, const FVector& Accel, const FVector& ClientWorldLocation, const FVector& RelativeClientLocation, UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode);

	/* Process a move at the given time stamp, given the compressed flags representing various events that occurred (ie jump). */
	virtual void MoveAutonomous( float ClientTimeStamp, float DeltaTime, uint8 CompressedFlags, const FVector& NewAccel);

	/** Unpack compressed flags from a saved move and set state accordingly. See FSavedMove_RPP. */
	virtual void UpdateFromCompressedFlags(uint8 Flags);

	/** Return true if it is OK to delay sending this player movement to the server, in order to conserve bandwidth. */
	virtual bool CanDelaySendingMove(const FSavedMovePtr& NewMove);

	/** Determine minimum delay between sending client updates to the server. If updates occur more frequently this than this time, moves may be combined delayed. */
	virtual float GetClientNetSendDeltaTime(const APlayerController* PC, const FNetworkPredictionData_Client_RPP* ClientData, const FSavedMovePtr& NewMove) const;

	/** Ticks the characters pose and accumulates root motion */
	void TickCharacterPose(float DeltaTime);

	/** On the server if we know we are having our replication rate throttled, this method checks if important replicated properties have changed that should cause us to return to the normal replication rate. */
	virtual bool ShouldCancelAdaptiveReplication() const;

public:

	/** React to instantaneous change in position. Invalidates cached floor recomputes it if possible if there is a current movement base. */
	virtual void UpdateFloorFromAdjustment();

	/** Minimum time between client TimeStamp resets.
	 !! This has to be large enough so that we don't confuse the server if the client can stall or timeout.
	 We do this as we use floats for TimeStamps, and server derives DeltaTime from two TimeStamps. 
	 As time goes on, accuracy decreases from those floating point numbers.
	 So we trigger a TimeStamp reset at regular intervals to maintain a high level of accuracy. */
	UPROPERTY()
	float MinTimeBetweenTimeStampResets;

	/** On the Server, verify that an incoming client TimeStamp is valid and has not yet expired.
		It will also handle TimeStamp resets if it detects a gap larger than MinTimeBetweenTimeStampResets / 2.f
		!! ServerData.CurrentClientTimeStamp can be reset !!
		@returns true if TimeStamp is valid, or false if it has expired. */
	virtual bool VerifyClientTimeStamp(float TimeStamp, FNetworkPredictionData_Server_RPP & ServerData);
protected:
	/** Internal const check for client timestamp validity without side-effects. 
	  * @see VerifyClientTimeStamp */
	bool IsClientTimeStampValid(float TimeStamp, const FNetworkPredictionData_Server_RPP& ServerData, bool& bTimeStampResetDetected) const;

	/** Called by UCharacterMovementComponent::VerifyClientTimeStamp() when a client timestamp reset has been detected and is valid. */
	virtual void OnClientTimeStampResetDetected();

	/** 
	 * Processes client timestamps from ServerMoves, detects and protects against time discrepancy between client-reported times and server time
	 * Called by UCharacterMovementComponent::VerifyClientTimeStamp() for valid timestamps.
	 */
	virtual void ProcessClientTimeStampForTimeDiscrepancy(float ClientTimeStamp, FNetworkPredictionData_Server_RPP& ServerData);

	/** 
	 * Called by UCharacterMovementComponent::ProcessClientTimeStampForTimeDiscrepancy() (on server) when the time from client moves 
	 * significantly differs from the server time, indicating potential time manipulation by clients (speed hacks, significant network 
	 * issues, client performance problems) 
	 * @param CurrentTimeDiscrepancy		Accumulated time difference between client ServerMove and server time - this is bounded
	 *										by MovementTimeDiscrepancy config variables in AGameNetworkManager, and is the value with which
	 *										we test against to trigger this function. This is reset when MovementTimeDiscrepancy resolution
	 *										is enabled
	 * @param LifetimeRawTimeDiscrepancy	Accumulated time difference between client ServerMove and server time - this is unbounded
	 *										and does NOT get affected by MovementTimeDiscrepancy resolution, and is useful as a longer-term
	 *										view of how the given client is performing. High magnitude unbounded error points to
	 *										intentional tampering by a client vs. occasional "naturally caused" spikes in error due to
	 *										burst packet loss/performance hitches
	 * @param Lifetime						Game time over which LifetimeRawTimeDiscrepancy has accrued (useful for determining severity
	 *										of LifetimeUnboundedError)
	 * @param CurrentMoveError				Time difference between client ServerMove and how much time has passed on the server for the
	 *										current move that has caused TimeDiscrepancy to accumulate enough to trigger detection.
	 */
	virtual void OnTimeDiscrepancyDetected(float CurrentTimeDiscrepancy, float LifetimeRawTimeDiscrepancy, float Lifetime, float CurrentMoveError);


public:

	////////////////////////////////////
	// Network RPCs for movement
	////////////////////////////////////

	/** Replicated function sent by client to server - contains client movement and view info. */
	UFUNCTION(unreliable, server, WithValidation)
	virtual void ServerMove(float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, uint8 CompressedMoveFlags, uint8 ClientRoll, uint32 View, UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode);
	virtual void ServerMove_Implementation(float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, uint8 CompressedMoveFlags, uint8 ClientRoll, uint32 View, UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode);
	virtual bool ServerMove_Validate(float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, uint8 CompressedMoveFlags, uint8 ClientRoll, uint32 View, UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode);

	/** Replicated function sent by client to server - contains client movement and view info for two moves. */
	UFUNCTION(unreliable, server, WithValidation)
	virtual void ServerMoveDual(float TimeStamp0, FVector_NetQuantize10 InAccel0, uint8 PendingFlags, uint32 View0, float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, uint8 NewFlags, uint8 ClientRoll, uint32 View, UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode);
	virtual void ServerMoveDual_Implementation(float TimeStamp0, FVector_NetQuantize10 InAccel0, uint8 PendingFlags, uint32 View0, float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, uint8 NewFlags, uint8 ClientRoll, uint32 View, UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode);
	virtual bool ServerMoveDual_Validate(float TimeStamp0, FVector_NetQuantize10 InAccel0, uint8 PendingFlags, uint32 View0, float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, uint8 NewFlags, uint8 ClientRoll, uint32 View, UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode);

	/** Replicated function sent by client to server - contains client movement and view info for two moves. First move is non root motion, second is root motion. */
	UFUNCTION(unreliable, server, WithValidation)
	virtual void ServerMoveDualHybridRootMotion(float TimeStamp0, FVector_NetQuantize10 InAccel0, uint8 PendingFlags, uint32 View0, float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, uint8 NewFlags, uint8 ClientRoll, uint32 View, UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode);
	virtual void ServerMoveDualHybridRootMotion_Implementation(float TimeStamp0, FVector_NetQuantize10 InAccel0, uint8 PendingFlags, uint32 View0, float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, uint8 NewFlags, uint8 ClientRoll, uint32 View, UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode);
	virtual bool ServerMoveDualHybridRootMotion_Validate(float TimeStamp0, FVector_NetQuantize10 InAccel0, uint8 PendingFlags, uint32 View0, float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, uint8 NewFlags, uint8 ClientRoll, uint32 View, UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode);

	/* Resending an (important) old move. Process it if not already processed. */
	UFUNCTION(unreliable, server, WithValidation)
	virtual void ServerMoveOld(float OldTimeStamp, FVector_NetQuantize10 OldAccel, uint8 OldMoveFlags);
	virtual void ServerMoveOld_Implementation(float OldTimeStamp, FVector_NetQuantize10 OldAccel, uint8 OldMoveFlags);
	virtual bool ServerMoveOld_Validate(float OldTimeStamp, FVector_NetQuantize10 OldAccel, uint8 OldMoveFlags);
	
	/** If no client adjustment is needed after processing received ServerMove(), ack the good move so client can remove it from SavedMoves */
	UFUNCTION(unreliable, client)
	virtual void ClientAckGoodMove(float TimeStamp);
	virtual void ClientAckGoodMove_Implementation(float TimeStamp);

	/** Replicate position correction to client, associated with a timestamped servermove.  Client will replay subsequent moves after applying adjustment.  */
	UFUNCTION(unreliable, client)
	virtual void ClientAdjustPosition(float TimeStamp, FVector NewLoc, FVector NewVel, UPrimitiveComponent* NewBase, FName NewBaseBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode);
	virtual void ClientAdjustPosition_Implementation(float TimeStamp, FVector NewLoc, FVector NewVel, UPrimitiveComponent* NewBase, FName NewBaseBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode);

	/* Bandwidth saving version, when velocity is zeroed */
	UFUNCTION(unreliable, client)
	virtual void ClientVeryShortAdjustPosition(float TimeStamp, FVector NewLoc, UPrimitiveComponent* NewBase, FName NewBaseBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode);
	virtual void ClientVeryShortAdjustPosition_Implementation(float TimeStamp, FVector NewLoc, UPrimitiveComponent* NewBase, FName NewBaseBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode);
	
	/** Replicate position correction to client when using root motion for movement. (animation root motion specific) */
	UFUNCTION(unreliable, client)
	void ClientAdjustRootMotionPosition(float TimeStamp, float ServerMontageTrackPosition, FVector ServerLoc, FVector_NetQuantizeNormal ServerRotation, float ServerVelZ, UPrimitiveComponent* ServerBase, FName ServerBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode);
	void ClientAdjustRootMotionPosition_Implementation(float TimeStamp, float ServerMontageTrackPosition, FVector ServerLoc, FVector_NetQuantizeNormal ServerRotation, float ServerVelZ, UPrimitiveComponent* ServerBase, FName ServerBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode);

	/** Replicate root motion source correction to client when using root motion for movement. */
	UFUNCTION(unreliable, client)
	void ClientAdjustRootMotionSourcePosition(float TimeStamp, FRootMotionSourceGroup ServerRootMotion, bool bHasAnimRootMotion, float ServerMontageTrackPosition, FVector ServerLoc, FVector_NetQuantizeNormal ServerRotation, float ServerVelZ, UPrimitiveComponent* ServerBase, FName ServerBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode);
	void ClientAdjustRootMotionSourcePosition_Implementation(float TimeStamp, FRootMotionSourceGroup ServerRootMotion, bool bHasAnimRootMotion, float ServerMontageTrackPosition, FVector ServerLoc, FVector_NetQuantizeNormal ServerRotation, float ServerVelZ, UPrimitiveComponent* ServerBase, FName ServerBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode);

protected:

	/** Event notification when client receives a correction from the server. Base implementation logs relevant data and draws debug info if "p.NetShowCorrections" is not equal to 0. */
	virtual void OnClientCorrectionReceived(class FNetworkPredictionData_Client_RPP& ClientData, float TimeStamp, FVector NewLocation, FVector NewVelocity, UPrimitiveComponent* NewBase, FName NewBaseBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode);
	
#endif
};

class REMOTEPLAY_API FNetworkPredictionData_Client_RPP : public FNetworkPredictionData_Client, protected FNoncopyable
{
public:
	FNetworkPredictionData_Client_RPP(const URemotePlayPawnMovementComponent& ClientMovement);
	virtual ~FNetworkPredictionData_Client_RPP();
};


class REMOTEPLAY_API FNetworkPredictionData_Server_RPP : public FNetworkPredictionData_Server, protected FNoncopyable
{
public:

	FNetworkPredictionData_Server_RPP(const URemotePlayPawnMovementComponent& ServerMovement);
	virtual ~FNetworkPredictionData_Server_RPP();
};

