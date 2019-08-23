#pragma once

#include "CoreMinimal.h"
#include "CoreTypes.h"
#include "Misc/Variant.h"
#include "UObject/ObjectMacros.h"
#include "GameFramework/Actor.h"

#include "RemotePlayMonitor.generated.h"

// A runtime actor to enable control and monitoring of the global RemotePlay state.
UCLASS(Blueprintable, hidecategories = (Object,Actor,Rendering,Replication,Input,Actor,Collision,LOD,Cooking) )
class ARemotePlayMonitor : public AActor
{
	GENERATED_BODY()
protected:
	virtual ~ARemotePlayMonitor();
public:
	ARemotePlayMonitor(const class FObjectInitializer& ObjectInitializer);

	/// Create or get the singleton instance of RemotePlayMonitor for the given UWorld.
	static ARemotePlayMonitor* Instantiate(UWorld* world);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RemotePlay)
	FString SessionName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RemotePlay)
	FString ClientIP;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RemotePlay)
	int32 VideoEncodeFrequency;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RemotePlay)
	uint32 StreamGeometry : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Debugging)
	int32 DebugStream;

	// In order:
	virtual void PostInitProperties() override;
	virtual void PostLoad() override;
	virtual void PostRegisterAllComponents() override;
	virtual void PostInitializeComponents() override;
private:
	static TMap<UWorld*, ARemotePlayMonitor*> Monitors;
};

