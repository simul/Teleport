#include "RemotePlayMonitor.h"

#include "Engine/Classes/Components/MeshComponent.h"
#include "Engine/Classes/Components/SceneCaptureComponentCube.h"
#include "Engine/Light.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextureRenderTarget.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

#include "Components/StreamableGeometryComponent.h"
#include "GeometrySource.h"
#include "RemotePlay.h"
#include "RemotePlaySettings.h"

TMap<UWorld*, ARemotePlayMonitor*> ARemotePlayMonitor::Monitors;
SCServer::CasterSettings ARemotePlayMonitor::Settings;

ARemotePlayMonitor::ARemotePlayMonitor(const class FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer), DetectionSphereRadius(1000), DetectionSphereBufferDistance(200), HandActor(nullptr),
	GeometryTicksPerSecond(2), GeometryBufferCutoffSize(1048576) /*1MB*/, ConfirmationWaitTime(15.0f), EstimatedDecodingFrequency(10)
{
	RequiredLatencyMs = 30;
	// Defaults from settings class.
	const URemotePlaySettings* RemotePlaySettings = GetDefault<URemotePlaySettings>();
	if (RemotePlaySettings)
	{
		ClientIP = RemotePlaySettings->ClientIP;
		VideoEncodeFrequency = RemotePlaySettings->VideoEncodeFrequency;
		bStreamGeometry = RemotePlaySettings->StreamGeometry;
	}
	else
	{
		VideoEncodeFrequency = 2;
		bStreamGeometry = true;
	}
	bStreamVideo = true;
	bOverrideTextureTarget = false;
	SceneCaptureTextureTarget = nullptr;
	bDeferOutput = false;
	bDoCubemapCulling = false;
	BlocksPerCubeFaceAcross = 2;
	TargetFPS = 60;
	CullQuadIndex = -1;
	IDRInterval = 0; // Value of 0 means only first frame will be IDR
	VideoCodec = VideoCodec::HEVC;
	RateControlMode = EncoderRateControlMode::RC_CBR_LOWDELAY_HQ;
	AverageBitrate = 40000000; // 40mb/s
	MaxBitrate = 80000000; // 80mb/s
	bAutoBitRate = false;
	vbvBufferSizeInFrames = 3;
	bUseAsyncEncoding = true;
	bUse10BitEncoding = false;
	bUseYUV444Decoding = false;

	DebugStream = 0;
	DebugNetworkPackets = false;
	DebugControlPackets=false;
	Checksums = false;
	ResetCache = false;

	UseCompressedTextures = true;
	QualityLevel = 1;
	CompressionLevel = 1;

	ExpectedLag = 0;

	bDisableMainCamera = false;
}


ARemotePlayMonitor::~ARemotePlayMonitor()
{
	auto *world = GetWorld();
	if(Monitors.Contains(world))
		Monitors.FindAndRemoveChecked(world);
}

ARemotePlayMonitor* ARemotePlayMonitor::Instantiate( UWorld* world)
{
	auto i = Monitors.Find(world);
	if (i)
	{
		return *i;
	}
	ARemotePlayMonitor* M = nullptr;
	TArray<AActor*> MActors;
	UGameplayStatics::GetAllActorsOfClass(world, ARemotePlayMonitor::StaticClass(), MActors);
	if (MActors.Num() > 0)
	{
		M = static_cast<ARemotePlayMonitor*>(MActors[0]);
	}
	else
	{
		UClass* monitorClass = ARemotePlayMonitor::StaticClass();
		M= world->SpawnActor<ARemotePlayMonitor>(monitorClass, FVector(0.0f, 0.f, 0.f), FRotator(0.0f, 0.f, 0.f), FActorSpawnParameters());
		Monitors.Add(TTuple<UWorld*, ARemotePlayMonitor*>(world, M));
	}
	return M;
}

void ARemotePlayMonitor::PostInitProperties()
{
	Super::PostInitProperties();
	bNetLoadOnClient = false;
	bCanBeDamaged = false;
	bRelevantForLevelBounds = false;
}

void ARemotePlayMonitor::PostLoad()
{
	Super::PostLoad();
}

void ARemotePlayMonitor::PostRegisterAllComponents()
{
	UpdateCasterSettings();
}

void ARemotePlayMonitor::PostInitializeComponents()
{
	Super::PostInitializeComponents();
}

void ARemotePlayMonitor::BeginPlay()
{
	Super::BeginPlay();

	ServerID = avs::GenerateUid();

	//Decompose the geometry in the level, if we are streaming the geometry.
	if(bStreamGeometry)
	{
		InitialiseGeometrySource();
	}
}

void ARemotePlayMonitor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	//We want to update when a value is set, not when they are dragging to their desired value.
	if(PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		if(Settings.enableGeometryStreaming == false && bStreamGeometry == true)
		{
			InitialiseGeometrySource();
		}

		UpdateCasterSettings();
	}
}

void ARemotePlayMonitor::UpdateCasterSettings()
{
	Settings = SCServer::CasterSettings
	{
		RequiredLatencyMs,

		*SessionName,
		*ClientIP,
		DetectionSphereRadius,
		DetectionSphereBufferDistance,
		ExpectedLag,
		ThrottleKpS,
		HandActor,

		bStreamGeometry,
		GeometryTicksPerSecond,
		GeometryBufferCutoffSize,
		ConfirmationWaitTime,

		bStreamVideo,
		bOverrideTextureTarget ? SceneCaptureTextureTarget->GetSurfaceWidth() : 0U,
		VideoEncodeFrequency,
		bDeferOutput,
		bDoCubemapCulling,
		BlocksPerCubeFaceAcross,
		CullQuadIndex,
		TargetFPS,
		IDRInterval,
		(avs::VideoCodec)VideoCodec,
		(SCServer::VideoEncoderRateControlMode)RateControlMode,
		AverageBitrate,
		MaxBitrate,
		bAutoBitRate,
		vbvBufferSizeInFrames,
		bUseAsyncEncoding,
		bUse10BitEncoding,
		bUseYUV444Decoding,

		DebugStream,
		DebugNetworkPackets,
		DebugControlPackets,
		Checksums,
		ResetCache,
		EstimatedDecodingFrequency,

		UseCompressedTextures,
		QualityLevel,
		CompressionLevel,

		bDisableMainCamera,

		avs::AxesStandard::UnrealStyle
	};
}

void ARemotePlayMonitor::InitialiseGeometrySource()
{
	UWorld* world = GetWorld();

	GeometrySource* geometrySource = IRemotePlay::Get().GetGeometrySource();
	geometrySource->Initialise(this, world);

	TArray<AActor*> staticMeshActors;
	UGameplayStatics::GetAllActorsOfClass(world, AStaticMeshActor::StaticClass(), staticMeshActors);

	TArray<AActor*> lightActors;
	UGameplayStatics::GetAllActorsOfClass(world, ALight::StaticClass(), lightActors);

	ECollisionChannel remotePlayChannel;
	FCollisionResponseParams profileResponses;
	//Returns the collision channel used by RemotePlay; uses the object type of the profile to determine the channel.
	UCollisionProfile::GetChannelAndResponseParams("RemotePlaySensor", remotePlayChannel, profileResponses);

	//Decompose all relevant actors into streamable geometry.
	for(auto actor : staticMeshActors)
	{
		UMeshComponent* rootMesh = Cast<UMeshComponent>(actor->GetComponentByClass(UMeshComponent::StaticClass()));

		//Decompose the meshes that would cause an overlap event to occur with the "RemotePlaySensor" profile.
		if(rootMesh->GetGenerateOverlapEvents() && rootMesh->GetCollisionResponseToChannel(remotePlayChannel) != ECollisionResponse::ECR_Ignore)
		{
			geometrySource->AddNode(rootMesh, true);
		}
	}

	//Decompose all relevant light actors into streamable geometry.
	for(auto actor : lightActors)
	{
		auto sgc = actor->GetComponentByClass(UStreamableGeometryComponent::StaticClass());
		if(sgc)
		{
			//TArray<UTexture2D*> shadowAndLightMaps = static_cast<UStreamableGeometryComponent*>(sgc)->GetLightAndShadowMaps();
			ULightComponent* lightComponent = static_cast<UStreamableGeometryComponent*>(sgc)->GetLightComponent();
			if(lightComponent)
			{
				//ShadowMapData smd(lc);
				geometrySource->AddNode(lightComponent, true);
			}
		}
	}

	geometrySource->CompressTextures();
}
