#pragma once

#include "CoreMinimal.h"
#include "CoreTypes.h"
#include "Misc/Variant.h"
#include "UObject/ObjectMacros.h"
#include "GameFramework/Actor.h"

#include "RemotePlayMonitor.generated.h"

UCLASS(Blueprintable)
class ARemotePlayMonitor : public AActor
{
	GENERATED_BODY()
protected:
	virtual ~ARemotePlayMonitor();
public:
	ARemotePlayMonitor(const class FObjectInitializer& ObjectInitializer);

	/// Create or get the singleton instance of RemotePlayMonitor for the given UWorld.
	static ARemotePlayMonitor* Instantiate(UWorld* world);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RemotePlay")
	FString SessionName;

private:
	static TMap<UWorld*, ARemotePlayMonitor*> Monitors;
};

