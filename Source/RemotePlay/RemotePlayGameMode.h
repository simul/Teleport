
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "GameFramework/GameModeBase.h"
#include "Misc/Guid.h"
#include "Templates/SubclassOf.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Info.h"
#include "UObject/CoreOnline.h"
#include "RemotePlayGameMode.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogRemotePlay, Log, All);

/**
* 
*/
UCLASS()
class REMOTEPLAY_API ARemotePlayGameMode : public AGameModeBase
{
	GENERATED_UCLASS_BODY()

	virtual APlayerController* SpawnPlayerController(ENetRole InRemoteRole, FVector const& SpawnLocation, FRotator const& SpawnRotation) override;
	UFUNCTION(BlueprintCallable, Category=Game)
	virtual void RestartPlayer(AController* NewPlayer) override;

	/** Tries to spawn the player's pawn at the specified actor's location */
	UFUNCTION(BlueprintCallable, Category=Game)
	virtual void RestartPlayerAtPlayerStart(AController* NewPlayer, AActor* StartSpot) override;

	/** Tries to spawn the player's pawn at a specific location */
	UFUNCTION(BlueprintCallable, Category=Game)
	virtual void RestartPlayerAtTransform(AController* NewPlayer, const FTransform& SpawnTransform) override;
public:
	/** The class of PlayerController to spawn for players logging in. */
	UPROPERTY( NoClear, BlueprintReadOnly, Category=Classes)
	TSubclassOf<APlayerController> ServerPlayerControllerClass;

	/** A PlayerState of this class will be associated with every player to replicate relevant player information to all clients. */
	UPROPERTY( NoClear, BlueprintReadOnly, Category=Classes)
	TSubclassOf<APlayerState> ServerPlayerStateClass;

};
