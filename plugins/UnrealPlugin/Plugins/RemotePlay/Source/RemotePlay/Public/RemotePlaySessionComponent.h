// Copyright 2018 Simul.co

#pragma once
#include "Components/ActorComponent.h"
#include "RemotePlaySessionComponent.generated.h"

class APawn;
class APlayerController;

typedef struct _ENetHost ENetHost;
typedef struct _ENetPeer ENetPeer;

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
	void StartSession(int32 ListenPort);

	UFUNCTION(BlueprintCallable, Category=RemotePlay)
	void StopSession();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category=RemotePlay)
	uint32 bAutoStartSession:1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category=RemotePlay)
	int32 AutoListenPort;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category=RemotePlay)
	int32 DisconnectTimeout;

private:
	void SwitchPlayerPawn(APawn* NewPawn);

	inline bool    Client_SendCommand(const FString& Cmd) const;
	inline FString Client_GetIPAddress() const;
	inline uint16  Client_GetPort() const;

	TWeakObjectPtr<APlayerController> PlayerController;
	TWeakObjectPtr<APawn> PlayerPawn;

	ENetHost* ServerHost;
	ENetPeer* ClientPeer;
};
