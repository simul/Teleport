#include "RemotePlayMonitor.h"
#include "Engine/World.h"

TMap<UWorld*, ARemotePlayMonitor*> ARemotePlayMonitor::Monitors;

ARemotePlayMonitor::ARemotePlayMonitor(const class FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}


ARemotePlayMonitor::~ARemotePlayMonitor()
{
	Monitors.FindAndRemoveChecked(this->GetWorld());
}

ARemotePlayMonitor* ARemotePlayMonitor::Instantiate( UWorld* world)
{
	auto i = Monitors.Find(world);
	if (i)
	{
		return *i;
	}
	UClass* monitorClass = ARemotePlayMonitor::StaticClass();
	ARemotePlayMonitor* M=world->SpawnActor<ARemotePlayMonitor>(monitorClass, FVector(0.0f, 0.f, 0.f), FRotator(0.0f, 0.f, 0.f), FActorSpawnParameters());
	Monitors.Add(TTuple<UWorld*,ARemotePlayMonitor*>( world,M ));
	return M;
}
