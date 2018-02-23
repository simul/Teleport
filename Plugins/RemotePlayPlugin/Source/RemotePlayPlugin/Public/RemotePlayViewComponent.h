// (c) 2018 Simul.co

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "RemotePlayViewComponent.generated.h"

UCLASS(meta=(BlueprintSpawnableComponent))
class REMOTEPLAYPLUGIN_API URemotePlayViewComponent : public UActorComponent
{
	GENERATED_BODY()
public:	
	URemotePlayViewComponent();

	/* Begin UActorComponent interface */
	virtual void UninitializeComponent() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	/* End UActorComponent interface */

	UFUNCTION(BlueprintCallable, Category="RemotePlay")
	void StartStreamingViewFromServer();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RemotePlay")
	UTextureRenderTarget2D* TextureTarget;

private:
	class FViewContext* Context;
	static void BeginInitializeContext(class FViewContext* InContext, const FString& InAddr);
	static void BeginReleaseContext(class FViewContext* InContext);
	void BeginProcessFrame(class FViewContext* InContext) const;
};
