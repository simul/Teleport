// (c) 2018 Simul.co

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SpectatorPawn.h"
#include "RemotePlaySpectatorPawn.generated.h"

UCLASS()
class REMOTEPLAY_API ARemotePlaySpectatorPawn : public ASpectatorPawn
{
	GENERATED_BODY()
public:
	ARemotePlaySpectatorPawn();

	/* Begin AActor interface */
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	/* End AActor interface */

	UFUNCTION(BlueprintCallable, Category="RemotePlay")
	void SetClientPawn(APawn* InClientPawn);
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="RemotePlay")
	class UCameraComponent* CameraComponent;

private:
	UPROPERTY()
	APawn* ClientPawn;
};
