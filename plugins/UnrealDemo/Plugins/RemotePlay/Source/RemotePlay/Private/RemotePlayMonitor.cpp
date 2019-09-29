#include "RemotePlayMonitor.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "RemotePlaySettings.h"

TMap<UWorld*, ARemotePlayMonitor*> ARemotePlayMonitor::Monitors;

ARemotePlayMonitor::ARemotePlayMonitor(const class FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer), HandActor(nullptr), GeometryTicksPerSecond(2), GeometryBufferCutoffSize(102400) /*100MB*/
{
	// Defaults from settings class.
	const URemotePlaySettings *RemotePlaySettings = GetDefault<URemotePlaySettings>();
	if (RemotePlaySettings)
	{
		ClientIP = RemotePlaySettings->ClientIP;
		VideoEncodeFrequency = RemotePlaySettings->VideoEncodeFrequency;
		StreamGeometry = RemotePlaySettings->StreamGeometry;
		StreamGeometryContinuously = RemotePlaySettings->StreamGeometryContinuously;
	}
	else
	{
		VideoEncodeFrequency = 2;
		StreamGeometry = true;
		StreamGeometryContinuously = true;
		StreamGeometryContinuously = false;
	}
	DeferOutput = false;
	DetectionSphereRadius = 500;
	DetectionSphereBufferDistance = 200;

	DebugStream = 0;
	Checksums = false;
	ResetCache = false;

	UseCompressedTextures = true;
	QualityLevel = 1;
	CompressionLevel = 1;

	ExpectedLag = 1;

	bUseAsyncEncoding = true;

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
}

void ARemotePlayMonitor::PostInitializeComponents()
{
	Super::PostInitializeComponents();
}

void ARemotePlayMonitor::BeginPlay()
{
	server_id = avs::GenerateUid();
}
