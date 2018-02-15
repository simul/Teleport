// (c) 2018 Simul.co

#include "RemotePlayFunctionLibrary.h"
#include "RemotePlay.h"

#include "Engine/LevelStreaming.h"
#include "Kismet/GameplayStatics.h"

void URemotePlayFunctionLibrary::SetStreamingLevelVisibility(UObject* WorldContextObject, const FName LevelName, bool bVisible)
{
	ULevelStreaming* LevelStreamingObject = UGameplayStatics::GetStreamingLevel(WorldContextObject, LevelName);
	if(LevelStreamingObject)
	{
		ULevel* Level = LevelStreamingObject->GetLoadedLevel();
		if(!Level)
		{
			UE_LOG(LogRemotePlay, Error, TEXT("Failed to set streaming level visibility: Level has not yet been loaded"));
			return;
		}

		for(int32 ActorIndex=0; ActorIndex<Level->Actors.Num(); ++ActorIndex)
		{
			AActor* Actor = Level->Actors[ActorIndex];
			Actor->SetActorHiddenInGame(!bVisible);
		}
	}
	else
	{
		UE_LOG(LogRemotePlay, Error, TEXT("Failed to set streaming level visibility: Level %s does not exist"), *LevelName.ToString());
	}
}
