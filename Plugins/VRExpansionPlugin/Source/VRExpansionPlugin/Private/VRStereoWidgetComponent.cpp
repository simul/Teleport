// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "VRStereoWidgetComponent.h"
#include "VRExpansionFunctionLibrary.h"
#include "TextureResource.h"
#include "Engine/Texture.h"
#include "IStereoLayers.h"
#include "IHeadMountedDisplay.h"
#include "PrimitiveViewRelevance.h"
#include "PrimitiveSceneProxy.h"
#include "UObject/ConstructorHelpers.h"
#include "EngineGlobals.h"
#include "MaterialShared.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Engine.h"
//#include "Widgets/SWindow.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Kismet/KismetSystemLibrary.h"
//#include "Input/HittestGrid.h"
//#include "SceneManagement.h"
#include "DynamicMeshBuilder.h"
//#include "PhysicsEngine/BoxElem.h"
#include "PhysicsEngine/BodySetup.h"
#include "Slate/SGameLayerManager.h"
#include "Slate/WidgetRenderer.h"
#include "Slate/SWorldWidgetScreenLayer.h"
#include "SViewport.h"

// CVars
namespace StereoWidgetCvars
{
	static int32 ForceNoStereoWithVRWidgets = 0;
	FAutoConsoleVariableRef CVarForceNoStereoWithVRWidgets(
		TEXT("vr.ForceNoStereoWithVRWidgets"),
		ForceNoStereoWithVRWidgets,
		TEXT("When on, will not allow stereo widget components to use stereo layers, will instead fall back to default widget rendering.\n")
		TEXT("0: Disable, 1: Enable"),
		ECVF_Default);
}

  //=============================================================================
UVRStereoWidgetComponent::UVRStereoWidgetComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
//	, bLiveTexture(false)
	//, bSupportsDepth(true)
	, bNoAlphaChannel(false)
	//, Texture(nullptr)
	//, LeftTexture(nullptr)
	, bQuadPreserveTextureRatio(false)
	//, StereoLayerQuadSize(FVector2D(500.0f, 500.0f))
	, UVRect(FBox2D(FVector2D(0.0f, 0.0f), FVector2D(1.0f, 1.0f)))
	//, CylinderRadius(100)
	//, CylinderOverlayArc(100)
	//, CylinderHeight(50)
	//, StereoLayerType(SLT_TrackerLocked)
	//, StereoLayerShape(SLSH_QuadLayer)
	, Priority(0)
	, bIsDirty(true)
	, bTextureNeedsUpdate(false)
	, LayerId(0)
	, LastTransform(FTransform::Identity)
	, bLastVisible(false)
{
	bShouldCreateProxy = true;
	bLastWidgetDrew = false;
	bUseEpicsWorldLockedStereo = false;
	// Replace quad size with DrawSize instead
	//StereoLayerQuadSize = DrawSize;

	//Texture = nullptr;
}

//=============================================================================
UVRStereoWidgetComponent::~UVRStereoWidgetComponent()
{
}

void UVRStereoWidgetComponent::BeginDestroy()
{
	IStereoLayers* StereoLayers;
	if (LayerId && GEngine->StereoRenderingDevice.IsValid() && (StereoLayers = GEngine->StereoRenderingDevice->GetStereoLayers()) != nullptr)
	{
		StereoLayers->DestroyLayer(LayerId);
		LayerId = 0;
	}

	Super::BeginDestroy();
}


void UVRStereoWidgetComponent::OnUnregister()
{
	IStereoLayers* StereoLayers;
	if (LayerId && GEngine->StereoRenderingDevice.IsValid() && (StereoLayers = GEngine->StereoRenderingDevice->GetStereoLayers()) != nullptr)
	{
		StereoLayers->DestroyLayer(LayerId);
		LayerId = 0;
	}

	Super::OnUnregister();
}

void UVRStereoWidgetComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{

	// Precaching what the widget uses for draw time here as it gets modified in the super tick
	bool bWidgetDrew = ShouldDrawWidget();

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (StereoWidgetCvars::ForceNoStereoWithVRWidgets)
	{
		if (!bShouldCreateProxy)
		{
			bShouldCreateProxy = true;
			MarkRenderStateDirty(); // Recreate
			if (LayerId)
			{
				if (GEngine->StereoRenderingDevice.IsValid())
				{
					IStereoLayers* StereoLayers = GEngine->StereoRenderingDevice->GetStereoLayers();
					if (StereoLayers)
						StereoLayers->DestroyLayer(LayerId);
				}
				LayerId = 0;
			}
		}

		return;
	}

	if (!UVRExpansionFunctionLibrary::IsInVREditorPreviewOrGame() || !GEngine->StereoRenderingDevice.IsValid() || (GEngine->StereoRenderingDevice->GetStereoLayers() == nullptr))
	{
		if (!bShouldCreateProxy)
		{
			bShouldCreateProxy = true;
			MarkRenderStateDirty(); // Recreate
			if (LayerId)
			{
				if (GEngine->StereoRenderingDevice.IsValid())
				{
					IStereoLayers* StereoLayers = GEngine->StereoRenderingDevice->GetStereoLayers();
					if (StereoLayers)
						StereoLayers->DestroyLayer(LayerId);
				}
				LayerId = 0;
			}
		}
	}
	else
	{
		if (bShouldCreateProxy)
		{
			bShouldCreateProxy = false;
			MarkRenderStateDirty(); // Recreate
		}
	}

#if !UE_SERVER

	// Same check that the widget runs prior to ticking
	if (IsRunningDedicatedServer() || (Widget == nullptr && !SlateWidget.IsValid()))
	{
		return;
	}

	IStereoLayers* StereoLayers;
	if (!UVRExpansionFunctionLibrary::IsInVREditorPreviewOrGame() || !GEngine->StereoRenderingDevice.IsValid() || (StereoLayers = GEngine->StereoRenderingDevice->GetStereoLayers()) == nullptr || !RenderTarget)
	{
		return;
	}

	FTransform Transform;
	// Never true until epic fixes back end code
	// #TODO: FIXME when they FIXIT (Slated 4.17)
	if (false)//StereoLayerType == SLT_WorldLocked)
	{
		Transform = GetComponentTransform();
	}
	else if (Space == EWidgetSpace::Screen)
	{
		Transform = GetRelativeTransform();
	}
	else // World locked here now
	{

		if (bUseEpicsWorldLockedStereo)
		{
			// Its incorrect......even in 4.17
			Transform = GetComponentTransform();
			Transform.ConcatenateRotation(FRotator(0.0f, -180.0f, 0.0f).Quaternion());
		}
		else
		{
			// Fix this when stereo world locked works again
			// Thanks to mitch for the temp work around idea

			// Get first local player controller
			APlayerController* PC = nullptr;
			for (FConstPlayerControllerIterator Iterator = GetWorld()->GetPlayerControllerIterator(); Iterator; ++Iterator)
			{
				if (Iterator->Get()->IsLocalPlayerController())
				{
					PC = Iterator->Get();
					break;
				}
			}

			if (PC)
			{
				APawn * mpawn = PC->GetPawnOrSpectator();
				//bTextureNeedsUpdate = true;
				if (mpawn)
				{
					// Set transform to this relative transform

					Transform = GetComponentTransform().GetRelativeTransform(mpawn->GetTransform());
					Transform.ConcatenateRotation(FRotator(0.0f, -180.0f, 0.0f).Quaternion());
					// I might need to inverse X axis here to get it facing the correct way, we'll see

					//Transform = mpawn->GetActorTransform().GetRelativeTransform(GetComponentTransform());
				}
			}
			else
			{
				// No PC, destroy the layer and enable drawing it normally.
				bShouldCreateProxy = true;

				if (LayerId)
				{
					StereoLayers->DestroyLayer(LayerId);
					LayerId = 0;
				}
				return;
			}
			//
			//Transform = GetRelativeTransform();
		}
	}

	// If the transform changed dirty the layer and push the new transform
	if (!bIsDirty && (bLastVisible != bVisible || bWidgetDrew != bLastWidgetDrew || FMemory::Memcmp(&LastTransform, &Transform, sizeof(Transform)) != 0))
	{
		bIsDirty = true;
	}

	bool bCurrVisible = bVisible;
	if (!RenderTarget || !RenderTarget->Resource || !bWidgetDrew)
	{
		bCurrVisible = false;
	}

	bLastWidgetDrew = bWidgetDrew;

	if (bIsDirty)
	{
		if (!bCurrVisible)
		{
			if (LayerId)
			{
				StereoLayers->DestroyLayer(LayerId);
				LayerId = 0;
			}
		}
		else
		{
			IStereoLayers::FLayerDesc LayerDsec;
			LayerDsec.Priority = Priority;
			LayerDsec.QuadSize = FVector2D(DrawSize);//StereoLayerQuadSize;

			/*if (DrawSize.X != DrawSize.Y)
			{
				// This might be a SteamVR only thing, it appears to always make the quad the largest of the two on the back end
				if (DrawSize.X > DrawSize.Y) 
					LayerDsec.QuadSize.Y = LayerDsec.QuadSize.X;
				else
					LayerDsec.QuadSize.X = LayerDsec.QuadSize.Y;
			}*/

			LayerDsec.UVRect = UVRect;
			LayerDsec.Transform = Transform;
			if (RenderTarget)
			{
				LayerDsec.Texture = RenderTarget->Resource->TextureRHI;
			}
			// Forget the left texture implementation
			//if (LeftTexture)
			//{
			//	LayerDsec.LeftTexture = LeftTexture->Resource->TextureRHI;
			//}


			const float ArcAngleRadians = FMath::DegreesToRadians(CylinderArcAngle);
			const float Radius = GetDrawSize().X / ArcAngleRadians;

			//LayerDsec.CylinderSize = FVector2D(/*CylinderRadius*/Radius, /*CylinderOverlayArc*/CylinderArcAngle);
			LayerDsec.CylinderRadius = Radius;
			LayerDsec.CylinderOverlayArc = CylinderArcAngle;

			// This needs to be auto set from variables, need to work on it
			LayerDsec.CylinderHeight = GetDrawSize().Y;//CylinderHeight;

			LayerDsec.Flags |= IStereoLayers::LAYER_FLAG_TEX_CONTINUOUS_UPDATE;// (/*bLiveTexture*/true) ? IStereoLayers::LAYER_FLAG_TEX_CONTINUOUS_UPDATE : 0;
			LayerDsec.Flags |= (bNoAlphaChannel) ? IStereoLayers::LAYER_FLAG_TEX_NO_ALPHA_CHANNEL : 0;
			LayerDsec.Flags |= (bQuadPreserveTextureRatio) ? IStereoLayers::LAYER_FLAG_QUAD_PRESERVE_TEX_RATIO : 0;
			//LayerDsec.Flags |= (bSupportsDepth) ? IStereoLayers::LAYER_FLAG_SUPPORT_DEPTH : 0;

			// Fix this later when WorldLocked is no longer wrong.
			switch (Space)
			{
			case EWidgetSpace::World:
			{
				if(bUseEpicsWorldLockedStereo)
					LayerDsec.PositionType = IStereoLayers::WorldLocked;
				else
					LayerDsec.PositionType = IStereoLayers::TrackerLocked;

				LayerDsec.Flags |= IStereoLayers::LAYER_FLAG_SUPPORT_DEPTH;
			}break;

			case EWidgetSpace::Screen:
			default:
			{
				LayerDsec.PositionType = IStereoLayers::FaceLocked;
			}break;
			}

			/*switch (StereoLayerType)
			{
			case SLT_WorldLocked:
				LayerDsec.PositionType = IStereoLayers::WorldLocked;
				break;
			case SLT_TrackerLocked:
				LayerDsec.PositionType = IStereoLayers::TrackerLocked;
				break;
			case SLT_FaceLocked:
				LayerDsec.PositionType = IStereoLayers::FaceLocked;
				break;
			}*/

			switch (GeometryMode)
			{
			case EWidgetGeometryMode::Cylinder:
			{
				LayerDsec.ShapeType = IStereoLayers::CylinderLayer;
			}break;
			case EWidgetGeometryMode::Plane:
			default:
			{
				LayerDsec.ShapeType = IStereoLayers::QuadLayer;
			}break;
			}

			// Can't use the cubemap with widgets currently, maybe look into it?
			/*switch (StereoLayerShape)
			{
			case SLSH_QuadLayer:
				LayerDsec.ShapeType = IStereoLayers::QuadLayer;
				break;

			case SLSH_CylinderLayer:
				LayerDsec.ShapeType = IStereoLayers::CylinderLayer;
				break;

			case SLSH_CubemapLayer:
				LayerDsec.ShapeType = IStereoLayers::CubemapLayer;
				break;
			default:
				break;
			}*/

			if (LayerId)
			{
				StereoLayers->SetLayerDesc(LayerId, LayerDsec);
			}
			else
			{
				LayerId = StereoLayers->CreateLayer(LayerDsec);
			}
		}
		LastTransform = Transform;
		bLastVisible = bCurrVisible;
		bIsDirty = false;
	}

	if (bTextureNeedsUpdate && LayerId)
	{
		StereoLayers->MarkTextureForUpdate(LayerId);
		bTextureNeedsUpdate = false;
	}
#endif
}


void UVRStereoWidgetComponent::SetPriority(int32 InPriority)
{
	if (Priority == InPriority)
	{
		return;
	}

	Priority = InPriority;
	bIsDirty = true;
}

void UVRStereoWidgetComponent::UpdateRenderTarget(FIntPoint DesiredRenderTargetSize)
{
	Super::UpdateRenderTarget(DesiredRenderTargetSize);
}

/** Represents a billboard sprite to the scene manager. */
class FStereoWidget3DSceneProxy : public FPrimitiveSceneProxy
{
public:
	/** Initialization constructor. */
	FStereoWidget3DSceneProxy(UVRStereoWidgetComponent* InComponent, ISlate3DRenderer& InRenderer)
		: FPrimitiveSceneProxy(InComponent)
		, Pivot(InComponent->GetPivot())
		, Renderer(InRenderer)
		, RenderTarget(InComponent->GetRenderTarget())
		, MaterialInstance(InComponent->GetMaterialInstance())
		, BodySetup(InComponent->GetBodySetup())
		, BlendMode(InComponent->GetBlendMode())
		, GeometryMode(InComponent->GetGeometryMode())
		, ArcAngle(FMath::DegreesToRadians(InComponent->GetCylinderArcAngle()))
	{
		bWillEverBeLit = false;
		bCreateSceneProxy = InComponent->bShouldCreateProxy;
		MaterialRelevance = MaterialInstance->GetRelevance(GetScene().GetFeatureLevel());
	}

	// FPrimitiveSceneProxy interface.
	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override
	{
		if(!bCreateSceneProxy)
			return;

#if WITH_EDITOR
		const bool bWireframe = AllowDebugViewmodes() && ViewFamily.EngineShowFlags.Wireframe;

		auto WireframeMaterialInstance = new FColoredMaterialRenderProxy(
			GEngine->WireframeMaterial ? GEngine->WireframeMaterial->GetRenderProxy(IsSelected()) : nullptr,
			FLinearColor(0, 0.5f, 1.f)
		);

		Collector.RegisterOneFrameMaterialProxy(WireframeMaterialInstance);

		FMaterialRenderProxy* ParentMaterialProxy = nullptr;
		if (bWireframe)
		{
			ParentMaterialProxy = WireframeMaterialInstance;
		}
		else
		{
			ParentMaterialProxy = MaterialInstance->GetRenderProxy(IsSelected());
		}
#else
		FMaterialRenderProxy* ParentMaterialProxy = MaterialInstance->GetRenderProxy(IsSelected());
#endif

		//FSpriteTextureOverrideRenderProxy* TextureOverrideMaterialProxy = new FSpriteTextureOverrideRenderProxy(ParentMaterialProxy,

		const FMatrix& ViewportLocalToWorld = GetLocalToWorld();

		if (RenderTarget)//false)//RenderTarget)
		{
			FTextureResource* TextureResource = RenderTarget->Resource;
			if (TextureResource)
			{
				if (GeometryMode == EWidgetGeometryMode::Plane)
				{
					float U = -RenderTarget->SizeX * Pivot.X;
					float V = -RenderTarget->SizeY * Pivot.Y;
					float UL = RenderTarget->SizeX * (1.0f - Pivot.X);
					float VL = RenderTarget->SizeY * (1.0f - Pivot.Y);

					int32 VertexIndices[4];

					for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
					{
						FDynamicMeshBuilder MeshBuilder;

						if (VisibilityMap & (1 << ViewIndex))
						{
							VertexIndices[0] = MeshBuilder.AddVertex(-FVector(0, U, V), FVector2D(0, 0), FVector(0, -1, 0), FVector(0, 0, -1), FVector(1, 0, 0), FColor::White);
							VertexIndices[1] = MeshBuilder.AddVertex(-FVector(0, U, VL), FVector2D(0, 1), FVector(0, -1, 0), FVector(0, 0, -1), FVector(1, 0, 0), FColor::White);
							VertexIndices[2] = MeshBuilder.AddVertex(-FVector(0, UL, VL), FVector2D(1, 1), FVector(0, -1, 0), FVector(0, 0, -1), FVector(1, 0, 0), FColor::White);
							VertexIndices[3] = MeshBuilder.AddVertex(-FVector(0, UL, V), FVector2D(1, 0), FVector(0, -1, 0), FVector(0, 0, -1), FVector(1, 0, 0), FColor::White);

							MeshBuilder.AddTriangle(VertexIndices[0], VertexIndices[1], VertexIndices[2]);
							MeshBuilder.AddTriangle(VertexIndices[0], VertexIndices[2], VertexIndices[3]);

							MeshBuilder.GetMesh(ViewportLocalToWorld, ParentMaterialProxy, SDPG_World, false, true, ViewIndex, Collector);
						}
					}
				}
				else
				{
					ensure(GeometryMode == EWidgetGeometryMode::Cylinder);

					const int32 NumSegments = FMath::Lerp(4, 32, ArcAngle / PI);


					const float Radius = RenderTarget->SizeX / ArcAngle;
					const float Apothem = Radius * FMath::Cos(0.5f*ArcAngle);
					const float ChordLength = 2.0f * Radius * FMath::Sin(0.5f*ArcAngle);

					const float PivotOffsetX = ChordLength * (0.5 - Pivot.X);
					const float V = -RenderTarget->SizeY * Pivot.Y;
					const float VL = RenderTarget->SizeY * (1.0f - Pivot.Y);

					int32 VertexIndices[4];

					for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
					{
						FDynamicMeshBuilder MeshBuilder;

						if (VisibilityMap & (1 << ViewIndex))
						{
							const float RadiansPerStep = ArcAngle / NumSegments;

							FVector LastTangentX;
							FVector LastTangentY;
							FVector LastTangentZ;

							for (int32 Segment = 0; Segment < NumSegments; Segment++)
							{
								const float Angle = -ArcAngle / 2 + Segment * RadiansPerStep;
								const float NextAngle = Angle + RadiansPerStep;

								// Polar to Cartesian
								const float X0 = Radius * FMath::Cos(Angle) - Apothem;
								const float Y0 = Radius * FMath::Sin(Angle);
								const float X1 = Radius * FMath::Cos(NextAngle) - Apothem;
								const float Y1 = Radius * FMath::Sin(NextAngle);

								const float U0 = static_cast<float>(Segment) / NumSegments;
								const float U1 = static_cast<float>(Segment + 1) / NumSegments;

								const FVector Vertex0 = -FVector(X0, PivotOffsetX + Y0, V);
								const FVector Vertex1 = -FVector(X0, PivotOffsetX + Y0, VL);
								const FVector Vertex2 = -FVector(X1, PivotOffsetX + Y1, VL);
								const FVector Vertex3 = -FVector(X1, PivotOffsetX + Y1, V);

								FVector TangentX = Vertex3 - Vertex0;
								TangentX.Normalize();
								FVector TangentY = Vertex1 - Vertex0;
								TangentY.Normalize();
								FVector TangentZ = FVector::CrossProduct(TangentX, TangentY);

								if (Segment == 0)
								{
									LastTangentX = TangentX;
									LastTangentY = TangentY;
									LastTangentZ = TangentZ;
								}

								VertexIndices[0] = MeshBuilder.AddVertex(Vertex0, FVector2D(U0, 0), LastTangentX, LastTangentY, LastTangentZ, FColor::White);
								VertexIndices[1] = MeshBuilder.AddVertex(Vertex1, FVector2D(U0, 1), LastTangentX, LastTangentY, LastTangentZ, FColor::White);
								VertexIndices[2] = MeshBuilder.AddVertex(Vertex2, FVector2D(U1, 1), TangentX, TangentY, TangentZ, FColor::White);
								VertexIndices[3] = MeshBuilder.AddVertex(Vertex3, FVector2D(U1, 0), TangentX, TangentY, TangentZ, FColor::White);

								MeshBuilder.AddTriangle(VertexIndices[0], VertexIndices[1], VertexIndices[2]);
								MeshBuilder.AddTriangle(VertexIndices[0], VertexIndices[2], VertexIndices[3]);

								LastTangentX = TangentX;
								LastTangentY = TangentY;
								LastTangentZ = TangentZ;
							}
							MeshBuilder.GetMesh(ViewportLocalToWorld, ParentMaterialProxy, SDPG_World, false, true, ViewIndex, Collector);
						}
					}
				}
			}
		}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			if (VisibilityMap & (1 << ViewIndex))
			{
				RenderCollision(BodySetup, Collector, ViewIndex, ViewFamily.EngineShowFlags, GetBounds(), IsSelected());
				RenderBounds(Collector.GetPDI(ViewIndex), ViewFamily.EngineShowFlags, GetBounds(), IsSelected());
			}
		}
#endif
	}

	void RenderCollision(UBodySetup* InBodySetup, FMeshElementCollector& Collector, int32 ViewIndex, const FEngineShowFlags& EngineShowFlags, const FBoxSphereBounds& InBounds, bool bRenderInEditor) const
	{
		if (InBodySetup)
		{
			bool bDrawCollision = EngineShowFlags.Collision && IsCollisionEnabled();

			if (bDrawCollision && AllowDebugViewmodes())
			{
				// Draw simple collision as wireframe if 'show collision', collision is enabled, and we are not using the complex as the simple
				const bool bDrawSimpleWireframeCollision = InBodySetup->CollisionTraceFlag != ECollisionTraceFlag::CTF_UseComplexAsSimple;

				if (FMath::Abs(GetLocalToWorld().Determinant()) < SMALL_NUMBER)
				{
					// Catch this here or otherwise GeomTransform below will assert
					// This spams so commented out
					//UE_LOG(LogStaticMesh, Log, TEXT("Zero scaling not supported (%s)"), *StaticMesh->GetPathName());
				}
				else
				{
					const bool bDrawSolid = !bDrawSimpleWireframeCollision;
					const bool bProxyIsSelected = IsSelected();

					if (bDrawSolid)
					{
						// Make a material for drawing solid collision stuff
						auto SolidMaterialInstance = new FColoredMaterialRenderProxy(
							GEngine->ShadedLevelColorationUnlitMaterial->GetRenderProxy(IsSelected(), IsHovered()),
							WireframeColor
						);

						Collector.RegisterOneFrameMaterialProxy(SolidMaterialInstance);

						FTransform GeomTransform(GetLocalToWorld());
						InBodySetup->AggGeom.GetAggGeom(GeomTransform, WireframeColor.ToFColor(true), SolidMaterialInstance, false, true, UseEditorDepthTest(), ViewIndex, Collector);
					}
					// wireframe
					else
					{
						FColor CollisionColor = FColor(157, 149, 223, 255);
						FTransform GeomTransform(GetLocalToWorld());
						InBodySetup->AggGeom.GetAggGeom(GeomTransform, GetSelectionColor(CollisionColor, bProxyIsSelected, IsHovered()).ToFColor(true), nullptr, false, false, UseEditorDepthTest(), ViewIndex, Collector);
					}
				}
			}
		}
	}

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
	{
		bool bVisible = true;

		FPrimitiveViewRelevance Result;

		MaterialRelevance.SetPrimitiveViewRelevance(Result);

		Result.bDrawRelevance = IsShown(View) && bVisible && View->Family->EngineShowFlags.WidgetComponents;
		Result.bDynamicRelevance = true;
		Result.bShadowRelevance = IsShadowCast(View);
		Result.bEditorPrimitiveRelevance = false;

		return Result;
	}

	virtual void GetLightRelevance(const FLightSceneProxy* LightSceneProxy, bool& bDynamic, bool& bRelevant, bool& bLightMapped, bool& bShadowMapped) const override
	{
		bDynamic = false;
		bRelevant = false;
		bLightMapped = false;
		bShadowMapped = false;
	}

	virtual void OnTransformChanged() override
	{
		Origin = GetLocalToWorld().GetOrigin();
	}

	virtual bool CanBeOccluded() const override
	{
		return !MaterialRelevance.bDisableDepthTest;
	}

	virtual uint32 GetMemoryFootprint(void) const override { return(sizeof(*this) + GetAllocatedSize()); }

	uint32 GetAllocatedSize(void) const { return(FPrimitiveSceneProxy::GetAllocatedSize()); }

private:
	FVector Origin;
	FVector2D Pivot;
	ISlate3DRenderer& Renderer;
	UTextureRenderTarget2D* RenderTarget;
	UMaterialInstanceDynamic* MaterialInstance;
	FMaterialRelevance MaterialRelevance;
	UBodySetup* BodySetup;
	EWidgetBlendMode BlendMode;
	EWidgetGeometryMode GeometryMode;
	float ArcAngle;
	bool bCreateSceneProxy;
};


FPrimitiveSceneProxy* UVRStereoWidgetComponent::CreateSceneProxy()
{
	// Always clear the material instance in case we're going from 3D to 2D.
	if (MaterialInstance)
	{
		MaterialInstance = nullptr;
	}
	
	if (Space != EWidgetSpace::Screen && WidgetRenderer.IsValid())
	{
		// Create a new MID for the current base material
		{
			UMaterialInterface* BaseMaterial = GetMaterial(0);

			MaterialInstance = UMaterialInstanceDynamic::Create(BaseMaterial, this);

			UpdateMaterialInstanceParameters();
		}

		RequestRedraw();
		LastWidgetRenderTime = 0;

		return new FStereoWidget3DSceneProxy(this, *WidgetRenderer->GetSlateRenderer());
	}

	return nullptr;
}


class FVRStereoWidgetComponentInstanceData : public FSceneComponentInstanceData
{
public:
	FVRStereoWidgetComponentInstanceData(const UVRStereoWidgetComponent* SourceComponent)
		: FSceneComponentInstanceData(SourceComponent)
		, WidgetClass(SourceComponent->GetWidgetClass())
		, RenderTarget(SourceComponent->GetRenderTarget())
	{}

	virtual void ApplyToComponent(UActorComponent* Component, const ECacheApplyPhase CacheApplyPhase) override
	{
		FSceneComponentInstanceData::ApplyToComponent(Component, CacheApplyPhase);
		CastChecked<UVRStereoWidgetComponent>(Component)->ApplyVRComponentInstanceData(this);
	}

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override
	{
		FSceneComponentInstanceData::AddReferencedObjects(Collector);

		UClass* WidgetUClass = *WidgetClass;
		Collector.AddReferencedObject(WidgetUClass);
		Collector.AddReferencedObject(RenderTarget);
	}

public:
	TSubclassOf<UUserWidget> WidgetClass;
	UTextureRenderTarget2D* RenderTarget;
};

FActorComponentInstanceData* UVRStereoWidgetComponent::GetComponentInstanceData() const
{
	return new FVRStereoWidgetComponentInstanceData(this);
}

void UVRStereoWidgetComponent::ApplyVRComponentInstanceData(FVRStereoWidgetComponentInstanceData* WidgetInstanceData)
{
	check(WidgetInstanceData);

	// Note: ApplyComponentInstanceData is called while the component is registered so the rendering thread is already using this component
	// That means all component state that is modified here must be mirrored on the scene proxy, which will be recreated to receive the changes later due to MarkRenderStateDirty.

	if (GetWidgetClass() != WidgetClass)
	{
		return;
	}

	RenderTarget = WidgetInstanceData->RenderTarget;

	// Also set the texture
	//Texture = RenderTarget;
	// Not needed anymore, just using the render target directly now

	if (MaterialInstance && RenderTarget)
	{
		MaterialInstance->SetTextureParameterValue("SlateUI", RenderTarget);
	}

	MarkRenderStateDirty();
}