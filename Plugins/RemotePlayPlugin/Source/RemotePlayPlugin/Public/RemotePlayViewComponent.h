// (c) 2018 Simul.co

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "RemotePlayTypes.h"
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
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="RemotePlay")
	TSubclassOf<AActor> DisplayActorClass;
	
	UPROPERTY(BlueprintReadOnly, Category="RemotePlay")
	AActor* DisplayActor;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="RemotePlay")
	FRemotePlayStreamParameters StreamParams;

private:
	class FViewContext* Context;
	void BeginInitializeContext(class FViewContext* InContext, const FString& InAddr) const;
	void BeginReleaseContext(class FViewContext* InContext) const;
	void BeginProcessFrame(class FViewContext* InContext) const;
};
