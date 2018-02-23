// (c) 2018 Simul.co

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneCaptureComponentCube.h"
#include "RemotePlayCaptureComponent.generated.h"

class FTextureRenderTargetResource;
 
UCLASS(hidecategories = (Collision, Object, Physics, SceneComponent), meta = (BlueprintSpawnableComponent))
class REMOTEPLAYPLUGIN_API URemotePlayCaptureComponent : public USceneCaptureComponentCube
{
	GENERATED_BODY()
public:
	URemotePlayCaptureComponent();

	/* Begin UActorComponent interface */
	virtual void UninitializeComponent() override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	/* End UActorComponent interface */

private:
	void OnViewportDrawn();
	FDelegateHandle ViewportDrawnDelegateHandle;

	class FCaptureContext* Context;
	static void BeginInitializeContext(class FCaptureContext* InContext);
	static void BeginReleaseContext(class FCaptureContext* InContext);
	void BeginCaptureFrame(class FCaptureContext* InContext) const;
};
