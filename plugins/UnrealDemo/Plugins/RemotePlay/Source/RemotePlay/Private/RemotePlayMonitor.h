#pragma once

#include "CoreMinimal.h"
#include "CoreTypes.h"
#include "Misc/Variant.h"
#include "UObject/ObjectMacros.h"
#include "GameFramework/Actor.h"

#include "libavstream/common.hpp" //uid

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
	uint32 DeferOutput : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RemotePlay)
	int32 DetectionSphereRadius;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RemotePlay)
	int32 DetectionSphereBufferDistance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RemotePlay)
	int32 ExpectedLag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RemotePlay)
	UBlueprint* HandActor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Encoding)
	uint32 StreamGeometry : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Encoding)
	uint32 StreamGeometryContinuously : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Encoding)
	uint8 GeometryTicksPerSecond;

	//Size we stop encoding nodes at.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Encoding)
	int32 GeometryBufferCutoffSize;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Encoding)
	uint32 bUseAsyncEncoding : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Debugging)
	int32 DebugStream;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Debugging)
	uint32 Checksums : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Debugging)
	uint32 ResetCache : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Compression)
	bool UseCompressedTextures;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Compression)
	uint8 QualityLevel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Compression)
	uint8 CompressionLevel;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Rendering)
	uint32 bDisableMainCamera : 1;


	// In order:
	virtual void PostInitProperties() override;
	virtual void PostLoad() override;
	virtual void PostRegisterAllComponents() override;
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;

	inline avs::uid GetServerID()
	{
		return server_id;
	}
private:
	static TMap<UWorld*, ARemotePlayMonitor*> Monitors;

	avs::uid server_id = 0; //UID of the server; resets between sessions.
};
