// Copyright 2018 Simul.co

#pragma once
#include "InputCoreTypes.h"
#include "Components/ActorComponent.h"
#include "RemotePlayDiscoveryService.h"
#include "GeometryStreamingService.h"

#include "SimulCasterServer/ClientMessaging.h"

#include "SessionComponent.generated.h"

class APawn;
class APlayerController;
class USphereComponent;

UCLASS(meta=(BlueprintSpawnableComponent))
class REMOTEPLAY_API URemotePlaySessionComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	URemotePlaySessionComponent();
	
	/* Begin UActorComponent interface */
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	/* End UActorComponent interface */

	UFUNCTION(BlueprintCallable, Category=RemotePlay)
	void StartSession(int32 ListenPort, int32 DiscoveryPort=0);

	UFUNCTION(BlueprintCallable, Category=RemotePlay)
	void StopSession();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category=RemotePlay)
	uint32 bAutoStartSession:1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category=RemotePlay)
	int32 AutoListenPort;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category=RemotePlay)
	int32 AutoDiscoveryPort;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category=RemotePlay)
	int32 DisconnectTimeout;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category=RemotePlay)
	float InputTouchSensitivity;

private:
	void SwitchPlayerPawn(APawn* NewPawn);
	void ReleasePlayerPawn();
	void ApplyPlayerInput(float DeltaTime);
	void SetHeadPose(const avs::HeadPose* newHeadPose);
	void ProcessNewInput(const avs::InputState* newInput);
	
	static void TranslateButtons(uint32_t ButtonMask, TArray<FKey>& OutKeys);
	void StartStreaming();
	void StopStreaming();

	UFUNCTION()
	void OnInnerSphereBeginOverlap(UPrimitiveComponent *OverlappedComponent, AActor *OtherActor, UPrimitiveComponent *OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult &SweepResult);

	UFUNCTION()
	void OnOuterSphereEndOverlap(UPrimitiveComponent *OverlappedComponent, AActor *OtherActor, UPrimitiveComponent *OtherComp, int32 OtherBodyIndex);

	void AddDetectionSpheres();

	std::shared_ptr<FGeometryStreamingService> GeometryStreamingService;
	std::shared_ptr<SCServer::DiscoveryService> DiscoveryService;
	std::unique_ptr<SCServer::ClientMessaging> ClientMessaging; //Handles client message receiving and reaction.
	struct SCServer::CasterContext* CasterContext;
	
	TWeakObjectPtr<APlayerController> PlayerController;
	TWeakObjectPtr<APawn> PlayerPawn;

	TWeakObjectPtr<USphereComponent> DetectionSphereInner; //Detects when a steamable actor has moved close enough to the client to be sent to them.
	TWeakObjectPtr<USphereComponent> DetectionSphereOuter; //Detects when a streamable actor has moved too far from the client.

	struct FInputQueue
	{
		TArray<FKey> ButtonsPressed;
		TArray<FKey> ButtonsReleased;
	};
	FInputQueue InputQueue;
	FVector2D   InputTouchAxis;
	FVector2D   InputJoystick;

	bool IsStreaming = false;

	
#if STATS || ENABLE_STATNAMEDEVENTS_UOBJECT
	/** Stat id of this object, 0 if nobody asked for it yet */
	mutable TStatId				BandwidthStatID;
	float						Bandwidth;
#endif // STATS || ENABLE_STATNAMEDEVENTS
	class ARemotePlayMonitor	*Monitor;
};
