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

	class FCaptureRenderContext* RenderContext;

	static void BeginInitializeRenderContext(class FCaptureRenderContext* InRenderContext);
	static void BeginReleaseRenderContext(class FCaptureRenderContext* InRenderContext);

	static void ProjectCapture_RenderThread(
		FRHICommandListImmediate& RHICmdList,
		class FCaptureRenderContext* RenderContext,
		FTextureRenderTargetResource* RenderTargetResource,
		ERHIFeatureLevel::Type FeatureLevel);
};
