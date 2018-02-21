// (c) 2018 Simul.co

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "RemotePlayController.generated.h"

UCLASS()
class REMOTEPLAY_API ARemotePlayController : public APlayerController
{
	GENERATED_BODY()
public:
	/* Begin AActor interface */
	virtual void BeginPlay() override;
	/* End AActor interface */

	/* Begin AController interface */
	virtual void Possess(APawn* Pawn) override;
	virtual void UnPossess() override;
	/* End AController interface */

private:
	void SetServerInputMode();
	void SetClientInputMode();
};
