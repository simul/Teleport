// Fill out your copyright notice in the Description page of Project Settings.

#include "RemotePlayPawnBase.h"
#include "Components/SceneCaptureComponentCube.h"
#include "Engine/TextureRenderTargetCube.h"

// Sets default values

ARemotePlayPawnBase::ARemotePlayPawnBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetRemoteRoleForBackwardsCompat(ROLE_SimulatedProxy);
 	// Set this pawn to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick	=true;
	//RootComponent	=CreateOptionalDefaultSubobject<USceneComponent>(TEXT("RootComponent"));
	//MeshComponent		=CreateOptionalDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
	//RemotePlayMovement				=CreateDefaultSubobject<URemotePlayPawnMovementComponent>("RemotePlayMovement");
	//SceneCaptureCube				=CreateOptionalDefaultSubobject<USceneCaptureComponentCube>(TEXT("SceneCaptureCube"));
	//SceneCaptureCube->SetupAttachment(MeshComponent);
	RemotePlayMovement = CreateDefaultSubobject<URemotePlayPawnMovementComponent>("RemotePlayMovement");
	MeshComponent = CreateOptionalDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent0"));

	SceneCaptureCube=nullptr;
}
// Called when the game starts or when spawned
void ARemotePlayPawnBase::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void ARemotePlayPawnBase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

// Called to bind functionality to input
void ARemotePlayPawnBase::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

}

void ARemotePlayPawnBase::InitializeCubemapCapture()
{
	FName YourObjectName("SceneCaptureCube");
	//CompClass can be a BP
	SceneCaptureCube = NewObject<USceneCaptureComponentCube>( this, YourObjectName);
	static FAttachmentTransformRules tra(EAttachmentRule::KeepRelative,EAttachmentRule::KeepWorld,EAttachmentRule::KeepRelative,false);
	SceneCaptureCube->AttachToComponent(RootComponent,tra);
	SceneCaptureCube->RegisterComponent();
	SceneCaptureCube->TextureTarget=NewObject<UTextureRenderTargetCube>(SceneCaptureCube,FName("CubeCaptureRT"));
	SceneCaptureCube->TextureTarget->Init(512, EPixelFormat::PF_FloatRGBA);
}

void ARemotePlayPawnBase::PostInitializeComponents()
{
#if WITH_SERVER_CODE
	InitializeCubemapCapture();
#endif // WITH_SERVER_CODE
	Super::PostInitializeComponents();
}

UPawnMovementComponent* ARemotePlayPawnBase::GetMovementComponent() const
{
	return RemotePlayMovement;
}

void ARemotePlayPawnBase::TurnOff()
{
	if (RemotePlayMovement != NULL)
	{
		RemotePlayMovement->StopMovementImmediately();
		//RemotePlayMovement->DisableMovement();
	}

	/*if (GetNetMode() != NM_DedicatedServer && Mesh != NULL)
	{
		Mesh->bPauseAnims = true;
		if (Mesh->IsSimulatingPhysics())
		{
			Mesh->bBlendPhysics = true;
			Mesh->KinematicBonesUpdateType = EKinematicBonesUpdateToPhysics::SkipAllBones;
		}
	}*/

	Super::TurnOff();
}

void ARemotePlayPawnBase::Restart()
{
	Super::Restart();

	if (RemotePlayMovement)
	{
		//RemotePlayMovement->SetDefaultMovementMode();
	}
}

void ARemotePlayPawnBase::PawnClientRestart()
{
	if (RemotePlayMovement != NULL)
	{
		RemotePlayMovement->StopMovementImmediately();
		RemotePlayMovement->ResetPredictionData_Client();
	}

	Super::PawnClientRestart();
}

void ARemotePlayPawnBase::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
}

void ARemotePlayPawnBase::UnPossessed()
{
	Super::UnPossessed();

	if (RemotePlayMovement)
	{
		RemotePlayMovement->ResetPredictionData_Client();
		RemotePlayMovement->ResetPredictionData_Server();
	}
}

void ARemotePlayPawnBase::DisplayDebug(UCanvas* Canvas, const FDebugDisplayInfo& DebugDisplay, float& YL, float& YPos)
{
	Super::DisplayDebug(Canvas, DebugDisplay, YL, YPos);

}

