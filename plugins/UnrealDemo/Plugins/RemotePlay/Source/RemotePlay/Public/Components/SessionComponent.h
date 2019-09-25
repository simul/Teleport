// Copyright 2018 Simul.co

#pragma once
#include "InputCoreTypes.h"
#include "Components/ActorComponent.h"
#include "RemotePlayDiscoveryService.h"
#include "GeometryStreamingService.h"
#include "SessionComponent.generated.h"

class APawn;
class APlayerController;
class USphereComponent;

typedef struct _ENetHost   ENetHost;
typedef struct _ENetPeer   ENetPeer;
typedef struct _ENetPacket ENetPacket;
typedef struct _ENetEvent  ENetEvent;

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

	void DispatchEvent(const ENetEvent& Event);
	void RecvHandshake(const ENetPacket* Packet);
	void RecvHeadPose(const ENetPacket* Packet);
	void RecvInput(const ENetPacket* Packet);

	bool			Client_SendCommand(const FString& Cmd) const;
	bool			Client_SendCommand(const avs::Command &avsSetup) const;
	inline FString	Client_GetIPAddress() const;
	inline uint16	Client_GetPort() const;
	
	static void TranslateButtons(uint32_t ButtonMask, TArray<FKey>& OutKeys);
	void StartStreaming();
	void StopStreaming();

	void StartGeometryStream();

	UFUNCTION()
	void OnInnerSphereBeginOverlap(UPrimitiveComponent *OverlappedComponent, AActor *OtherActor, UPrimitiveComponent *OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult &SweepResult);

	UFUNCTION()
	void OnOuterSphereEndOverlap(UPrimitiveComponent *OverlappedComponent, AActor *OtherActor, UPrimitiveComponent *OtherComp, int32 OtherBodyIndex);

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

	ENetHost* ServerHost;
	ENetPeer* ClientPeer;

	FRemotePlayDiscoveryService DiscoveryService;
	FGeometryStreamingService GeometryStreamingService;
	 
	struct FRemotePlayContext* RemotePlayContext;
#if STATS || ENABLE_STATNAMEDEVENTS_UOBJECT
	/** Stat id of this object, 0 if nobody asked for it yet */
	mutable TStatId				BandwidthStatID;
	float						Bandwidth;
#endif // STATS || ENABLE_STATNAMEDEVENTS
	class ARemotePlayMonitor	*Monitor;
};
