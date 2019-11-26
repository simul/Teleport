#pragma once

#include "CoreMinimal.h"
#include "CoreTypes.h"
#include "Misc/Variant.h"
#include "UObject/ObjectMacros.h"
#include "GameFramework/Actor.h"

#include "libavstream/common.hpp" //uid

#include "RemotePlayMonitor.generated.h"

/**
 * Rate Control Modes
 */
UENUM(BlueprintType)
enum class EncoderRateControlMode : uint8
{
	RC_CONSTQP = 0x0 UMETA(DisplayName = "Constant Quantization Parameter"), /**< Constant QP mode */
	RC_VBR = 0x1 UMETA(DisplayName = "Variable Bitrate"), /**< Variable bitrate mode */
	RC_CBR = 0x2 UMETA(DisplayName = "Constant Bitrate"), /**< Constant bitrate mode */
	RC_CBR_LOWDELAY_HQ = 0x3 UMETA(DisplayName = "Constant Bitrate Low Delay HQ"), /**< low-delay CBR, high quality */
	RC_CBR_HQ = 0x4 UMETA(DisplayName = "Constant Bitrate HQ (slower)"), /**< CBR, high quality (slower) */
	RC_VBR_HQ = 0x5 UMETA(DisplayName = "Variable Bitrate HQ (slower)") /**< VBR, high quality (slower) */
};

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
	int32 DetectionSphereRadius;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RemotePlay)
	int32 DetectionSphereBufferDistance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RemotePlay)
	int32 ExpectedLag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RemotePlay)
	UBlueprint* HandActor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Geometry)
	uint32 StreamGeometry : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Geometry)
	uint32 StreamGeometryContinuously : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Geometry)
	uint8 GeometryTicksPerSecond;

	// Size we stop encoding nodes at.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Geometry)
	int32 GeometryBufferCutoffSize;

	//Seconds to wait before resending a resource.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Geometry, meta = (ClampMin = "0.5", ClampMax = "300.0"))
	float ConfirmationWaitTime;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Encoding)
	uint32 bOverrideTextureTarget : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Encoding)
	class UTextureRenderTargetCube* SceneCaptureTextureTarget;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Encoding)
	int32 VideoEncodeFrequency;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Encoding)
	uint32 bDeferOutput : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Encoding)
	int32 TargetFPS;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Encoding)
	int32 IDRInterval;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Encoding)
	EncoderRateControlMode RateControlMode;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Encoding)
	int32 AverageBitrate;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Encoding)
	int32 MaxBitrate;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Encoding)
	uint32 bAutoBitRate : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Encoding)
	int32 vbvBufferSizeInFrames;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Encoding)
	uint32 bUseAsyncEncoding : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Encoding)
	uint32 bUse10BitEncoding : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Encoding)
	uint32 bUseYUV444Decoding : 1;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Debugging)
	int32 DebugStream;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Debugging)
	uint32 Checksums : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Debugging)
	uint32 ResetCache : 1;

	//An estimate of how frequently the client will decode the packets sent to it; used by throttling.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Debugging)
	uint8 EstimatedDecodingFrequency;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Compression)
	bool UseCompressedTextures;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Compression)
	uint8 QualityLevel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Compression)
	uint8 CompressionLevel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Camera)
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
