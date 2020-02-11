#include "GeometrySource.h"
#include "StreamableGeometryComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Rendering/StaticMeshVertexBuffer.h"
#include "Rendering/PositionVertexBuffer.h"
#include "StaticMeshResources.h"
#include "Engine/MapBuildDataRegistry.h"

//Basis Universal
#include "basisu_comp.h"
#include "transcoder/basisu_transcoder.h"

//Unreal File Manager
#include "Core/Public/HAL/FileManagerGeneric.h"

//Textures
#include "Engine/Classes/EditorFramework/AssetImportData.h"

//Materials & Material Expressions
#include "Engine/Classes/Materials/Material.h"
#include "Engine/Classes/Materials/MaterialExpressionConstant.h"
#include "Engine/Classes/Materials/MaterialExpressionConstant3Vector.h"
#include "Engine/Classes/Materials/MaterialExpressionConstant4Vector.h"
#include "Engine/Classes/Materials/MaterialExpressionScalarParameter.h"
#include "Engine/Classes/Materials/MaterialExpressionVectorParameter.h"
#include "Engine/Classes/Materials/MaterialExpressionTextureCoordinate.h"
#include "Engine/Classes/Materials/MaterialExpressionMultiply.h"

//For progress bar while compressing textures.
#include "ScopedSlowTask.h"
 
#include "RemotePlayMonitor.h"
 
#include <functional> //std::function
  
#if 0 
#include <random> 
std::default_random_engine generator; 
std::uniform_int_distribution<int> distribution(1, 6);
int dice_roll = distribution(generator);
#endif
#define LOG_MATERIAL_INTERFACE(materialInterface) UE_LOG(LogRemotePlay, Warning, TEXT("%s"), *("Decomposing <" + materialInterface->GetName() + ">: Error"));
#define LOG_UNSUPPORTED_MATERIAL_EXPRESSION(materialInterface, name) UE_LOG(LogRemotePlay, Warning, TEXT("%s"), *("Decomposing <" + materialInterface->GetName() + ">: Unsupported expression with type name <" + name + ">"));
#define LOG_UNSUPPORTED_MATERIAL_CHAIN_LENGTH(materialInterface, length) UE_LOG(LogRemotePlay, Warning, TEXT("%s"), *("Decomposing <" + materialInterface->GetName() + ">: Unsupported property chain length of <" + length + ">"));
 
struct GeometrySource::Mesh
{
	avs::uid id;
	UStaticMesh* staticMesh;
	FString bulkDataIDString; //ID string of the bulk data the last time it was processed; changes whenever the mesh data is reimported, so can be used to detect changes.
};

struct GeometrySource::MaterialChangedInfo
{
	avs::uid id = 0;
	bool wasProcessedThisSession = false;
};

namespace
{
	const unsigned long long DUMMY_TEX_COORD = 0;
}

FName GetUniqueComponentName(USceneComponent* component)
{
	return *FPaths::Combine(component->GetOutermost()->GetName(), component->GetOuter()->GetName(), component->GetName());
}

GeometrySource::GeometrySource()
	:Monitor(nullptr)
{}

GeometrySource::~GeometrySource()
{}

void GeometrySource::Initialise(ARemotePlayMonitor* monitor, UWorld* world)
{
	check(monitor);  

	Monitor = monitor;
	if(Monitor->ResetCache)
	{
		//Clear all stored data, if the reset cache flag is set.
		ClearData(); 
	}
	else
	{
		//Otherwise, flag all processed materials as not having been changed this session; so we will update them incase they changed, while also reusing IDs.
		for(auto& material : processedMaterials)
		{
			material.second.wasProcessedThisSession = false;
		}
	}
	
	UStaticMeshComponent* handMeshComponent = nullptr;
	//Use the hand actor blueprint set in the monitor.
	if(Monitor->HandActor)
	{
		AActor* handActor = world->SpawnActor(Monitor->HandActor->GeneratedClass);
		handMeshComponent = Cast<UStaticMeshComponent>(handActor->GetComponentByClass(UStaticMeshComponent::StaticClass()));

		if(!handMeshComponent)
		{
			UE_LOG(LogRemotePlay, Warning, TEXT("Hand actor set in RemotePlayMonitor has no static mesh component."));
		}
	}
	else
	{
		UE_LOG(LogRemotePlay, Log, TEXT("No hand actor set in RemotePlayMonitor."));
	}

	//If we can not use a set blueprint, then we use the default one.
	if(!handMeshComponent)
	{
		FString defaultHandLocation("Blueprint'/Game/RemotePlay/RemotePlayHand.RemotePlayHand'");
		UBlueprint* defaultHandBlueprint = Cast<UBlueprint>(StaticLoadObject(UBlueprint::StaticClass(), nullptr, *defaultHandLocation));

		if(defaultHandBlueprint)
		{
			AActor* handActor = world->SpawnActor(defaultHandBlueprint->GeneratedClass);
			handMeshComponent = Cast<UStaticMeshComponent>(handActor->GetComponentByClass(UStaticMeshComponent::StaticClass()));

			if(!handMeshComponent)
			{
				UE_LOG(LogRemotePlay, Warning, TEXT("Default hand actor in <%s> has no static mesh component."), *defaultHandLocation);
			}
		}
		else
		{
			UE_LOG(LogRemotePlay, Warning, TEXT("Could not find default hand actor in <%s>."), *defaultHandLocation);
		}
	}

	//Add the hand actors, and their resources, to the geometry source.
	if(handMeshComponent)
	{
		std::vector<std::pair<void*, avs::uid>> oldHands = storage.getHands();

		avs::uid firstHandID = AddNode(handMeshComponent, true);

		//Retrieve the newly added/updated hand node, and set to the correct data type.
		avs::DataNode* firstHand = storage.getNode(firstHandID);
		firstHand->data_type = avs::NodeDataType::Hand;

		//Whether we created a new node for the hand model, or it already existed; i.e whether the hand actor has changed.
		bool isSameHandNode = oldHands.size() != 0 && firstHandID == oldHands[0].second;

		//Set the ID of the second hand, and create second hand's node by copying the first hand.
		avs::uid secondHandID = isSameHandNode ? oldHands[1].second : avs::GenerateUid();
		storage.storeNode(secondHandID, avs::DataNode{*firstHand});

		///Use data nodes rather than actors for pointers; there's little else they can point to.
		storage.setHands({firstHand, firstHandID}, {storage.getNode(secondHandID), secondHandID});
	}
}

avs::AttributeSemantic IndexToSemantic(int index)
{
	switch (index)
	{
	case 0:
		return avs::AttributeSemantic::POSITION;
	case 1:
		return avs::AttributeSemantic::NORMAL;
	case 2:
		return avs::AttributeSemantic::TANGENT;
	case 3:
		return avs::AttributeSemantic::TEXCOORD_0;
	case 4:
		return avs::AttributeSemantic::TEXCOORD_1;
	case 5:
		return avs::AttributeSemantic::COLOR_0;
	case 6:
		return avs::AttributeSemantic::JOINTS_0;
	case 7:
		return avs::AttributeSemantic::WEIGHTS_0;
	};
	return avs::AttributeSemantic::TEXCOORD_0;
}

bool GeometrySource::InitMesh(Mesh* mesh, uint8 lodIndex)
{
	if (mesh->staticMesh->GetClass()->IsChildOf(USkeletalMesh::StaticClass()))
	{
		return false;
	}

	UStaticMesh *StaticMesh = Cast<UStaticMesh>(mesh->staticMesh);
	auto &lods = StaticMesh->RenderData->LODResources;
	if(!lods.Num()) return false;

	std::vector<avs::PrimitiveArray> primitiveArrays;
	std::unordered_map<avs::uid, avs::Accessor> accessors;
	std::map<avs::uid, avs::BufferView> bufferViews;
	std::map<avs::uid, avs::GeometryBuffer> buffers;

	auto AddBuffer = [this, &buffers](Mesh* mesh, avs::uid b_uid, size_t num, size_t stride, const void *data)
	{
		// Data may already exist:
		if (buffers.find(b_uid) == buffers.end())
		{
			avs::GeometryBuffer& b = buffers[b_uid];
			b.byteLength = num * stride;
			b.data = (const uint8_t *)data;			// Remember, just a pointer: we don't own this data.
		}
	};
	auto AddBufferView = [this, &bufferViews](Mesh* mesh, avs::uid b_uid, avs::uid v_uid, size_t start_index, size_t num, size_t stride)
	{
		avs::BufferView &bv = bufferViews[v_uid];
		bv.byteOffset = start_index * stride;
		bv.byteLength = num * stride;
		bv.byteStride = stride;
		bv.buffer = b_uid;
	};

	auto &lod = lods[lodIndex];
	FPositionVertexBuffer &pb = lod.VertexBuffers.PositionVertexBuffer;
	FStaticMeshVertexBuffer &vb = lod.VertexBuffers.StaticMeshVertexBuffer;

	avs::uid positions_uid = avs::GenerateUid();
	avs::uid normals_uid = avs::GenerateUid();
	avs::uid tangents_uid = avs::GenerateUid();
	avs::uid texcoords_uid = avs::GenerateUid();
	avs::uid indices_uid = avs::GenerateUid();
	avs::uid positions_view_uid = avs::GenerateUid();
	avs::uid normals_view_uid = avs::GenerateUid();
	avs::uid tangents_view_uid = avs::GenerateUid();
	avs::uid texcoords_view_uid[8];

	avs::uid indices_view_uid = avs::GenerateUid();
	avs::Accessor::ComponentType componentType;
	size_t istride;

	// First create the Buffers:
	// Position:
	{
		std::vector<avs::vec3> &p = scaledPositionBuffers[positions_uid];
		p.resize(pb.GetNumVertices());
		const avs::vec3 *orig = (const avs::vec3 *) pb.GetVertexData();
		for (size_t j = 0; j < pb.GetNumVertices(); j++)
		{
			p[j].x = orig[j].x *0.01f;
			p[j].y = orig[j].y *0.01f;
			p[j].z = orig[j].z *0.01f;
		}
		size_t stride = pb.GetStride();
		AddBuffer(mesh, positions_uid, pb.GetNumVertices(), stride, (const void*)p.data());
		size_t position_stride = pb.GetStride();
		// Offset is zero, because the sections are just lists of indices. 
		AddBufferView(mesh, positions_uid, positions_view_uid, 0, pb.GetNumVertices(), position_stride);
	}

	size_t tangent_stride = vb.GetTangentSize() / vb.GetNumVertices();
	// Normal:
	{
		size_t stride = vb.GetTangentSize() / vb.GetNumVertices();
		AddBuffer(mesh, normals_uid, vb.GetNumVertices(), stride, vb.GetTangentData());
		AddBufferView(mesh, normals_uid, normals_view_uid,  0, pb.GetNumVertices(), tangent_stride);
	}

	// Tangent:
	if (vb.GetTangentData())
	{
		size_t stride = vb.GetTangentSize() / vb.GetNumVertices();
		AddBuffer(mesh, tangents_uid, vb.GetNumVertices(), stride, vb.GetTangentData());
		AddBufferView(mesh, tangents_uid, tangents_view_uid, 0, pb.GetNumVertices(), tangent_stride);
	}

	// TexCoords:
	{
		std::vector<FVector2D>& uvData = processedUVs[texcoords_uid];
		uvData.reserve(vb.GetNumVertices() * vb.GetNumTexCoords());

		size_t texcoords_stride = sizeof(FVector2D);
		for(size_t j = 0; j < vb.GetNumTexCoords(); j++)
		{
			//bool IsFP32 = vb.GetUseFullPrecisionUVs(); //Not need vb.GetVertexUV() returns FP32 regardless. 

			for(uint32_t k = 0; k < vb.GetNumVertices(); k++)
			{
				uvData.push_back(vb.GetVertexUV(k, j));
			}
		}
		AddBuffer(mesh, texcoords_uid, vb.GetNumVertices() * vb.GetNumTexCoords(), texcoords_stride, uvData.data());
		for(size_t j = 0; j < vb.GetNumTexCoords(); j++)
		{
			//bool IsFP32 = vb.GetUseFullPrecisionUVs(); //Not need vb.GetVertexUV() returns FP32 regardless. 
			texcoords_view_uid[j] = avs::GenerateUid();
			AddBufferView(mesh, texcoords_uid, texcoords_view_uid[j], j * pb.GetNumVertices(), pb.GetNumVertices(), texcoords_stride);
		}
	}

	//Indices:
	{
		FRawStaticIndexBuffer& ib = lod.IndexBuffer;
		FIndexArrayView arr = ib.GetArrayView();

		componentType = ib.Is32Bit() ? avs::Accessor::ComponentType::UINT : avs::Accessor::ComponentType::USHORT;
		istride = avs::GetComponentSize(componentType);

		AddBuffer(mesh, indices_uid, ib.GetNumIndices(), istride, (const void*)((uint64*)&arr)[0]);
		AddBufferView(mesh, indices_uid, indices_view_uid, 0, ib.GetNumIndices(), istride);
	}
	
	// Now create the views:
	size_t  num_elements = lod.Sections.Num();
	primitiveArrays.resize(num_elements);
	for (size_t i = 0; i < num_elements; i++)
	{
		auto &section = lod.Sections[i];
		auto &pa = primitiveArrays[i];
		pa.attributeCount = 2 + (vb.GetTangentData() ? 1 : 0) + (vb.GetTexCoordData() ? vb.GetNumTexCoords() : 0);
		pa.attributes = new avs::Attribute[pa.attributeCount];
		size_t idx = 0;
		// Position:
		{
			avs::Attribute &attr = pa.attributes[idx++];
			attr.accessor = avs::GenerateUid();
			attr.semantic = avs::AttributeSemantic::POSITION;
			avs::Accessor &a = accessors[attr.accessor];
			a.byteOffset = 0;
			a.type = avs::Accessor::DataType::VEC3;
			a.componentType = avs::Accessor::ComponentType::FLOAT;
			a.count = pb.GetNumVertices();
			a.bufferView = positions_view_uid;
		}
		// Normal:
		{
			avs::Attribute &attr = pa.attributes[idx++];
			attr.accessor = avs::GenerateUid();
			attr.semantic = avs::AttributeSemantic::TANGENTNORMALXZ;
			avs::Accessor &a = accessors[attr.accessor];
			a.byteOffset = 0;
			a.type = avs::Accessor::DataType::VEC4;
			//GetUseHighPrecisionTangentBasis() ? PF_R16G16B16A16_SNORM : PF_R8G8B8A8_SNORM
			if (vb.GetUseHighPrecisionTangentBasis())
				a.componentType = avs::Accessor::ComponentType::UINT;
			else
				a.componentType = avs::Accessor::ComponentType::USHORT;
			a.count = vb.GetNumVertices();// same as pb???
			a.bufferView = normals_view_uid;
		}
		// Tangent:
		if (vb.GetTangentData())
		{
			avs::Attribute &attr = pa.attributes[idx++];
			attr.accessor = avs::GenerateUid();
			attr.semantic = avs::AttributeSemantic::TANGENT;
			avs::Accessor &a = accessors[attr.accessor];
			a.byteOffset = 0;
			a.type = avs::Accessor::DataType::VEC4;
			a.componentType = avs::Accessor::ComponentType::FLOAT;
			a.count = vb.GetTangentSize() / 8;// same as pb???
			a.bufferView = tangents_view_uid;
		}
		// TexCoords:
		for (size_t j = 0; j < vb.GetNumTexCoords(); j++)
		{
			avs::Attribute &attr = pa.attributes[idx++];
			attr.accessor = avs::GenerateUid();
			attr.semantic = j == 0 ? avs::AttributeSemantic::TEXCOORD_0 : avs::AttributeSemantic::TEXCOORD_1;
			avs::Accessor &a = accessors[attr.accessor];
			// Offset into the global texcoord views
			a.byteOffset = 0;
			a.type = avs::Accessor::DataType::VEC2;
			a.componentType = avs::Accessor::ComponentType::FLOAT;
			a.count = vb.GetNumVertices();// same as pb???
			a.bufferView = texcoords_view_uid[j];
		}

		//Indices:
		{
			pa.indices_accessor = avs::GenerateUid();

			avs::Accessor& i_a = accessors[pa.indices_accessor];
			i_a.byteOffset = section.FirstIndex * istride;
			i_a.type = avs::Accessor::DataType::SCALAR;
			i_a.componentType = componentType;
			i_a.count = section.NumTriangles * 3;// same as pb???
			i_a.bufferView = indices_view_uid;
		}

		// probably no default material in UE4?
		pa.material = 0;
		pa.primitiveMode = avs::PrimitiveMode::TRIANGLES;
	}

	storage.storeMesh(mesh->id, avs::Mesh{primitiveArrays, accessors, bufferViews, buffers});
	return true;
}

avs::uid GeometrySource::AddMeshNode(UMeshComponent* meshComponent, avs::uid oldID)
{
	avs::uid dataID = AddMesh(meshComponent);
	if(dataID == 0)
	{
		UE_LOG(LogRemotePlay, Warning, TEXT("Failed to add mesh for actor with name: %s"), *meshComponent->GetOuter()->GetName());
		return 0;
	}	

	//Materials that this mesh has applied to its material slots.
	TArray<UMaterialInterface*> mats = meshComponent->GetMaterials();

	std::vector<avs::uid> materialIDs;
	//Add material, and textures, for streaming to clients.
	for(int32 i = 0; i < mats.Num(); i++)
	{
		avs::uid materialID = AddMaterial(mats[i]);
		if(materialID != 0) materialIDs.push_back(materialID);
		else UE_LOG(LogRemotePlay, Warning, TEXT("Actor \"%s\" has no material applied to material slot %d."), *meshComponent->GetOuter()->GetName(), i);
	}

	TArray<USceneComponent*> children;
	meshComponent->GetChildrenComponents(false, children);

	std::vector<avs::uid> childIDs;
	for(auto child : children)
	{
		//If the oldID does not equal zero, then we are forcing an update on the hierarchy.
		avs::uid childID = AddNode(Cast<UMeshComponent>(child), oldID != 0);
		if(childID != 0) childIDs.push_back(childID);
		else UE_LOG(LogRemotePlay, Log, TEXT("Failed to add child component \"%s\" as a node of actor \"%s\""), *child->GetName(), *meshComponent->GetOuter()->GetName());
	}

	avs::uid nodeID = oldID == 0 ? avs::GenerateUid() : oldID;
	processedNodes[GetUniqueComponentName(meshComponent)] = nodeID;
	storage.storeNode(nodeID, avs::DataNode{GetComponentTransform(meshComponent), dataID, avs::NodeDataType::Mesh, materialIDs, childIDs});

	return nodeID;
}

avs::uid GeometrySource::AddShadowMapNode(ULightComponent* lightComponent, avs::uid oldID)
{
	avs::uid dataID = AddShadowMap(lightComponent->StaticShadowDepthMap.Data);
	if(dataID == 0)
	{
		UE_LOG(LogRemotePlay, Warning, TEXT("Failed to add shadow map for actor with name: %s"), *lightComponent->GetOuter()->GetName());
		return 0;
	}

	avs::uid nodeID = oldID == 0 ? avs::GenerateUid() : oldID;
	processedNodes[GetUniqueComponentName(lightComponent)] = nodeID;
	storage.storeNode(nodeID, avs::DataNode{GetComponentTransform(lightComponent), dataID, avs::NodeDataType::ShadowMap, {}, {}});

	return nodeID;
}

avs::Transform GeometrySource::GetComponentTransform(USceneComponent* component)
{
	check(component)

	FTransform transform = component->GetComponentTransform();

	// convert offset from cm to metres.
	FVector t = transform.GetTranslation() * 0.01f;
	// We retain Unreal axes until sending to individual clients, which might have varying standards.
	FQuat r = transform.GetRotation();
	const FVector s = transform.GetScale3D();

	return avs::Transform{t.X, t.Y, t.Z, r.X, r.Y, r.Z, r.W, s.X, s.Y, s.Z};
}

void GeometrySource::ClearData()
{
	scaledPositionBuffers.clear();
	processedUVs.clear();

	processedNodes.clear();
	processedMeshes.clear();
	processedMaterials.clear();
	processedTextures.clear();
	processedShadowMaps.clear();
}

avs::uid GeometrySource::AddMesh(UMeshComponent *MeshComponent)
{
	if (MeshComponent->GetClass()->IsChildOf(USkeletalMeshComponent::StaticClass()))
	{
		UE_LOG(LogRemotePlay, Warning, TEXT("Skeletal meshes not supported yet. Found on actor: %s"), *MeshComponent->GetOuter()->GetName());
		return 0;
	}

	UStaticMeshComponent* staticMeshComponent = Cast<UStaticMeshComponent>(MeshComponent);
	UStaticMesh *staticMesh = staticMeshComponent->GetStaticMesh();

	if(!staticMesh)
	{
		UE_LOG(LogRemotePlay, Warning, TEXT("Actor \"%s\" has been set as streamable, but they have no mesh assigned to their mesh component!"), *MeshComponent->GetOuter()->GetName());
		return 0;
	}

	//The mesh data was reimported, if the ID string has changed.
	FString idString;
	if(staticMesh->SourceModels.Num() != 0 && staticMesh->SourceModels[0].MeshDescriptionBulkData)
	{
		idString = staticMesh->SourceModels[0].MeshDescriptionBulkData->GetIdString();
	}

	Mesh* mesh;

	auto meshIt = processedMeshes.find(staticMesh);
	if(meshIt != processedMeshes.end())
	{
		//Reuse the ID if this mesh has been processed before.
		mesh = &meshIt->second;

		//Return if we have already processed the mesh in this play session, or the processed data wasn't cleared at the start of the play session, and the mesh data has not changed.
		if(idString == mesh->bulkDataIDString)
		{
			return mesh->id;
		}
	}
	else
	{
		//Create a new ID if this mesh has never been processed.
		mesh = &processedMeshes[staticMesh];
		mesh->id = avs::GenerateUid();
	}

	mesh->staticMesh = staticMesh;
	mesh->bulkDataIDString = idString;
	PrepareMesh(mesh);
	InitMesh(mesh, 0);
	
	return mesh->id;
}

avs::uid GeometrySource::AddNode(USceneComponent* component, bool forceUpdate)
{
	check(component);

	std::map<FName, avs::uid>::iterator nodeIterator = processedNodes.find(GetUniqueComponentName(component));

	avs::uid nodeID = nodeIterator != processedNodes.end() ? nodeIterator->second : 0;

	if(forceUpdate || nodeID == 0)
	{
		UMeshComponent* meshComponent = Cast<UMeshComponent>(component);
		ULightComponent* lightComponent = Cast<ULightComponent>(component);

		if(meshComponent) nodeID = AddMeshNode(meshComponent, nodeID);
		else if(lightComponent) nodeID = AddShadowMapNode(lightComponent, nodeID);
		else
		{
			UE_LOG(LogRemotePlay, Warning, TEXT("Actor \"%s\" was marked as streamable, but has no streamable component."), *component->GetOuter()->GetName());
		}
	}

	return nodeID;
}

avs::uid GeometrySource::AddMaterial(UMaterialInterface* materialInterface)
{
	//Return 0 if we were passed a nullptr.
	if(!materialInterface) return 0;
	
	//Try and locate the pointer in the list of processed materials.
	std::unordered_map<UMaterialInterface*, MaterialChangedInfo>::iterator materialIt = processedMaterials.find(materialInterface);

	avs::uid materialID;
	if(materialIt != processedMaterials.end())
	{
		//Reuse the ID if this material has been processed before.
		materialID = materialIt->second.id;

		//Return if we have already processed the material in this play session.
		if(materialIt->second.wasProcessedThisSession) return materialID;
	}
	else
	{
		materialID = avs::GenerateUid();
	}
	processedMaterials[materialInterface] = {materialID, true};

	avs::Material newMaterial;

	newMaterial.name = TCHAR_TO_ANSI(*materialInterface->GetName());

	DecomposeMaterialProperty(materialInterface, EMaterialProperty::MP_BaseColor, newMaterial.pbrMetallicRoughness.baseColorTexture, newMaterial.pbrMetallicRoughness.baseColorFactor);
	DecomposeMaterialProperty(materialInterface, EMaterialProperty::MP_Metallic, newMaterial.pbrMetallicRoughness.metallicRoughnessTexture, newMaterial.pbrMetallicRoughness.metallicFactor);
	DecomposeMaterialProperty(materialInterface, EMaterialProperty::MP_Roughness, newMaterial.pbrMetallicRoughness.metallicRoughnessTexture, newMaterial.pbrMetallicRoughness.roughnessFactor);
	DecomposeMaterialProperty(materialInterface, EMaterialProperty::MP_AmbientOcclusion, newMaterial.occlusionTexture, newMaterial.occlusionTexture.strength);
	DecomposeMaterialProperty(materialInterface, EMaterialProperty::MP_Normal, newMaterial.normalTexture, newMaterial.normalTexture.scale);
	DecomposeMaterialProperty(materialInterface, EMaterialProperty::MP_EmissiveColor, newMaterial.emissiveTexture, newMaterial.emissiveFactor);

	//MP_WorldPositionOffset Property Chain for SimpleGrassWind
	{
		TArray<UMaterialExpression*> outExpressions;
		materialInterface->GetMaterial()->GetExpressionsInPropertyChain(MP_WorldPositionOffset, outExpressions, nullptr);

		if(outExpressions.Num() != 0)
		{
			if(outExpressions[0]->GetName().Contains("MaterialFunctionCall"))
			{
				UMaterialExpressionMaterialFunctionCall* functionExp = Cast<UMaterialExpressionMaterialFunctionCall>(outExpressions[0]);
				if(functionExp->MaterialFunction->GetName() == "SimpleGrassWind")
				{
					avs::SimpleGrassWindExtension simpleGrassWind;

					if(functionExp->FunctionInputs[0].Input.Expression && functionExp->FunctionInputs[0].Input.Expression->GetName().Contains("Constant"))
					{
						simpleGrassWind.windIntensity = Cast<UMaterialExpressionConstant>(functionExp->FunctionInputs[0].Input.Expression)->R;
					}

					if(functionExp->FunctionInputs[1].Input.Expression && functionExp->FunctionInputs[1].Input.Expression->GetName().Contains("Constant"))
					{
						simpleGrassWind.windWeight = Cast<UMaterialExpressionConstant>(functionExp->FunctionInputs[1].Input.Expression)->R;
					}

					if(functionExp->FunctionInputs[2].Input.Expression && functionExp->FunctionInputs[2].Input.Expression->GetName().Contains("Constant"))
					{
						simpleGrassWind.windSpeed = Cast<UMaterialExpressionConstant>(functionExp->FunctionInputs[2].Input.Expression)->R;
					}

					if(functionExp->FunctionInputs[3].Input.Expression && functionExp->FunctionInputs[3].Input.Expression->GetName().Contains("TextureSample"))
					{
						simpleGrassWind.texUID = AddTexture(Cast<UMaterialExpressionTextureBase>(functionExp->FunctionInputs[3].Input.Expression)->Texture);
					}

					newMaterial.extensions[avs::MaterialExtensionIdentifier::SIMPLE_GRASS_WIND] = std::make_unique<avs::SimpleGrassWindExtension>(simpleGrassWind);
				}
			}
		}
	}

	storage.storeMaterial(materialID, std::move(newMaterial));

	UE_CLOG(newMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index != newMaterial.occlusionTexture.index, LogRemotePlay, Warning, TEXT("Occlusion texture on material <%s> is not combined with metallic-roughness texture."), *materialInterface->GetName());

	return materialID;
}

avs::uid GeometrySource::AddShadowMap(const FStaticShadowDepthMapData* shadowDepthMapData)
{
	//Check for nullptr
	if(!shadowDepthMapData) return 0;

	//Return pre-stored shadow_uid
	auto it = processedShadowMaps.find(shadowDepthMapData);
	if (it != processedShadowMaps.end())
	{
		return (it->second);
	}

	//Generate new shadow map
	avs::uid shadow_uid = avs::GenerateUid();
	avs::Texture shadowTexture;

	shadowTexture.name = std::string("Shadow Map UID: ") + std::to_string(shadow_uid);
	shadowTexture.width = shadowDepthMapData->ShadowMapSizeX;
	shadowTexture.height = shadowDepthMapData->ShadowMapSizeY;
	shadowTexture.depth = 1;
	shadowTexture.bytesPerPixel = shadowDepthMapData->DepthSamples.GetTypeSize();;
	shadowTexture.arrayCount = 1;
	shadowTexture.mipCount = 1;

	shadowTexture.format = shadowTexture.bytesPerPixel == 4 ? avs::TextureFormat::D32F : 
							shadowTexture.bytesPerPixel == 3 ? avs::TextureFormat::D24F : 
							shadowTexture.bytesPerPixel == 2 ? avs::TextureFormat::D16F :
							avs::TextureFormat::INVALID;
	shadowTexture.compression = avs::TextureCompression::UNCOMPRESSED;

	shadowTexture.dataSize = shadowDepthMapData->DepthSamples.GetAllocatedSize();
	shadowTexture.data = new unsigned char[shadowTexture.dataSize];
	memcpy(shadowTexture.data, (uint8_t*)shadowDepthMapData->DepthSamples.GetData(), shadowTexture.dataSize);
	shadowTexture.sampler_uid = 0;

	
	processedShadowMaps[shadowDepthMapData] = shadow_uid;
	storage.storeShadowMap(shadow_uid, std::move(shadowTexture));
	
	return shadow_uid;
}

void GeometrySource::CompressTextures()
{
	size_t texturesToCompressAmount = storage.getAmountOfTexturesWaitingForCompression();

#define LOCTEXT_NAMESPACE "GeometrySource"
	//Create FScopedSlowTask to show user progress of compressing the texture.
	FScopedSlowTask compressTextureTask(texturesToCompressAmount, FText::Format(LOCTEXT("Compressing Texture", "Starting compression of {0} textures"), texturesToCompressAmount));
	compressTextureTask.MakeDialog(false, true);

	for(int i = 0; i < texturesToCompressAmount; i++)
	{
		const avs::Texture* textureInfo = storage.getNextCompressedTexture();
		compressTextureTask.EnterProgressFrame(1.0f, FText::Format(LOCTEXT("Compressing Texture", "Compressing texture {0}/{1} ({2} [{3} x {4}])"), i + 1, texturesToCompressAmount, FText::FromString(ANSI_TO_TCHAR(textureInfo->name.data())), textureInfo->width, textureInfo->height));
		
		storage.compressNextTexture();
	}
#undef LOCTEXT_NAMESPACE
}

void GeometrySource::PrepareMesh(Mesh* mesh)
{
	// We will pre-encode the mesh to prepare it for streaming.
	if (mesh->staticMesh->GetClass()->IsChildOf(UStaticMesh::StaticClass()))
	{
		UStaticMesh* StaticMesh = mesh->staticMesh;
		int verts = StaticMesh->GetNumVertices(0);
		FStaticMeshRenderData *StaticMeshRenderData = StaticMesh->RenderData.Get();
		if (!StaticMeshRenderData->IsInitialized())
		{
			UE_LOG(LogRemotePlay, Warning, TEXT("StaticMeshRenderData Not ready"));
			return;
		}
		FStaticMeshLODResources &LODResources = StaticMeshRenderData->LODResources[0];

		FPositionVertexBuffer &PositionVertexBuffer = LODResources.VertexBuffers.PositionVertexBuffer;
		FStaticMeshVertexBuffer &StaticMeshVertexBuffer = LODResources.VertexBuffers.StaticMeshVertexBuffer;

		uint32 pos_stride = PositionVertexBuffer.GetStride();
		const float *pos_data = (const float*)PositionVertexBuffer.GetVertexData();

		int numVertices = PositionVertexBuffer.GetNumVertices();
		for (int i = 0; i < numVertices; i++)
		{
			pos_data += pos_stride / sizeof(float);
		}
	}
}

avs::uid GeometrySource::AddTexture(UTexture* texture)
{
	avs::uid textureID;
	auto it = processedTextures.find(texture);

	if(it != processedTextures.end())
	{
		//Reuse the ID if this texture has been processed before, and return value
		textureID = it->second;
		return textureID;
	}
	else
	{
		//Create a new ID if this texture has never been processed.
		textureID = avs::GenerateUid();
		processedTextures[texture] = textureID;
	}

	avs::Texture newTexture;

	//Assuming the first running platform is the desired running platform.
	FTexture2DMipMap baseMip = texture->GetRunningPlatformData()[0]->Mips[0];
	FTextureSource& textureSource = texture->Source;

	newTexture.name = TCHAR_TO_ANSI(*texture->GetName());
	newTexture.width = baseMip.SizeX;
	newTexture.height = baseMip.SizeY;
	newTexture.depth = baseMip.SizeZ; ///!!! Is this actually where Unreal stores its depth information for a texture? !!!
	newTexture.bytesPerPixel = textureSource.GetBytesPerPixel();
	newTexture.arrayCount = textureSource.GetNumSlices(); ///!!! Is this actually the array count? !!!
	newTexture.mipCount = textureSource.GetNumMips();
	newTexture.sampler_uid = 0;

	UE_CLOG(newTexture.bytesPerPixel != 4, LogRemotePlay, Warning, TEXT("Texture \"%s\" has bytes per pixel of %d!"), *texture->GetName(), newTexture.bytesPerPixel);

	switch(textureSource.GetFormat())
	{
		case ETextureSourceFormat::TSF_G8:
			newTexture.format = avs::TextureFormat::G8;
			break;
		case ETextureSourceFormat::TSF_BGRA8:
			newTexture.format = avs::TextureFormat::BGRA8;
			break;
		case ETextureSourceFormat::TSF_BGRE8:
			newTexture.format = avs::TextureFormat::BGRE8;
			break;
		case ETextureSourceFormat::TSF_RGBA16:
			newTexture.format = avs::TextureFormat::RGBA16;
			break;
		case ETextureSourceFormat::TSF_RGBA16F:
			newTexture.format = avs::TextureFormat::RGBA16F;
			break;
		case ETextureSourceFormat::TSF_RGBA8:
			newTexture.format = avs::TextureFormat::RGBA8;
			break;
		default:
			newTexture.format = avs::TextureFormat::INVALID;
			UE_LOG(LogRemotePlay, Warning, TEXT("Invalid texture format on texture: %s"), *texture->GetName());
			break;
	}

	TArray<uint8> mipData;
	textureSource.GetMipData(mipData, 0);

	newTexture.dataSize = newTexture.width * newTexture.height * newTexture.depth * newTexture.bytesPerPixel;
	newTexture.data = mipData.GetData();
	
	std::string basisFileLocation;
	if(Monitor->UseCompressedTextures)
	{
		FString GameSavedDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());

		//Create a unique name based on the filepath.
		FString uniqueName = FPaths::ConvertRelativePathToFull(texture->AssetImportData->SourceData.SourceFiles[0].RelativeFilename);
		uniqueName = uniqueName.Replace(TEXT("/"), TEXT("#")); //Replaces slashes with hashes.
		uniqueName = uniqueName.RightChop(2); //Remove drive.
		uniqueName = uniqueName.Right(255); //Restrict name length.

		basisFileLocation = TCHAR_TO_ANSI(*FPaths::Combine(GameSavedDir, uniqueName + FString(".basis")));
	}

	FDateTime textureTimestamp = texture->AssetImportData->SourceData.SourceFiles[0].Timestamp;
	storage.storeTexture(textureID, std::move(newTexture), textureTimestamp.ToUnixTimestamp(), basisFileLocation);

	return textureID;
}

void GeometrySource::GetDefaultTexture(UMaterialInterface *materialInterface, EMaterialProperty propertyChain, avs::TextureAccessor &outTexture)
{
	TArray<UTexture*> outTextures;
	materialInterface->GetTexturesInPropertyChain(propertyChain, outTextures, nullptr, nullptr);

	UTexture *texture = outTextures.Num() ? outTextures[0] : nullptr;
	
	if(texture)
	{
		outTexture = {AddTexture(texture), DUMMY_TEX_COORD};
	}
}

void GeometrySource::DecomposeMaterialProperty(UMaterialInterface *materialInterface, EMaterialProperty propertyChain, avs::TextureAccessor &outTexture, float &outFactor)
{
	TArray<UMaterialExpression*> outExpressions;
	materialInterface->GetMaterial()->GetExpressionsInPropertyChain(propertyChain, outExpressions, nullptr);

	if(outExpressions.Num() != 0)
	{
		std::function<size_t(size_t)> expressionDecomposer = [&](size_t expressionIndex)
		{
			size_t expressionsHandled = 1;
			if (expressionIndex >= outExpressions.Num())
			{
				LOG_MATERIAL_INTERFACE(materialInterface);
				return size_t(0);
			}
			FString name = outExpressions[expressionIndex]->GetName();

			if(name.Contains("Multiply"))
			{
				expressionsHandled += expressionDecomposer(expressionIndex + expressionsHandled);
				expressionsHandled += expressionDecomposer(expressionIndex + expressionsHandled);
			}
			else if(name.Contains("TextureSample"))
			{
				expressionsHandled += DecomposeTextureSampleExpression(materialInterface, Cast<UMaterialExpressionTextureSample>(outExpressions[expressionIndex]), outTexture);
			}
			else if(name.Contains("ConstantBiasScale"))
			{
				LOG_UNSUPPORTED_MATERIAL_EXPRESSION(materialInterface, name);

				if(outTexture.index == 0)
				{
					GetDefaultTexture(materialInterface, propertyChain, outTexture);
				}
			}
			else if(name.Contains("Constant"))
			{
				outFactor = Cast<UMaterialExpressionConstant>(outExpressions[expressionIndex])->R;
			}
			else if(name.Contains("ScalarParameter"))
			{
				///INFO: Just using the parameter's name won't work for layered materials.
				materialInterface->GetScalarParameterValue(outExpressions[expressionIndex]->GetParameterName(), outFactor);
			}
			else
			{
				LOG_UNSUPPORTED_MATERIAL_EXPRESSION(materialInterface, name);

				if(outTexture.index == 0)
				{
					GetDefaultTexture(materialInterface, propertyChain, outTexture);
				}
			}

			return expressionsHandled;
		};

		expressionDecomposer(0);
	}
}

void GeometrySource::DecomposeMaterialProperty(UMaterialInterface *materialInterface, EMaterialProperty propertyChain, avs::TextureAccessor &outTexture, avs::vec3 &outFactor)
{
	TArray<UMaterialExpression*> outExpressions;
	materialInterface->GetMaterial()->GetExpressionsInPropertyChain(propertyChain, outExpressions, nullptr);

	if(outExpressions.Num() != 0)
	{
		std::function<size_t(size_t)> expressionDecomposer = [&](size_t expressionIndex)
		{
			size_t expressionsHandled = 1;
			FString name = outExpressions[expressionIndex]->GetName();

			if(name.Contains("Multiply"))
			{
				expressionsHandled += expressionDecomposer(expressionIndex + expressionsHandled);
				expressionsHandled += expressionDecomposer(expressionIndex + expressionsHandled);
			}
			else if(name.Contains("TextureSample"))
			{
				expressionsHandled += DecomposeTextureSampleExpression(materialInterface, Cast<UMaterialExpressionTextureSample>(outExpressions[expressionIndex]), outTexture);
			}
			else if(name.Contains("Constant3Vector"))
			{
				FLinearColor colour = Cast<UMaterialExpressionConstant3Vector>(outExpressions[expressionIndex])->Constant;
				outFactor = {colour.R, colour.G, colour.B};
			}
			else if(name.Contains("VectorParameter"))
			{
				FLinearColor colour;
				///INFO: Just using the parameter's name won't work for layered materials.
				materialInterface->GetVectorParameterValue(outExpressions[expressionIndex]->GetParameterName(), colour);

				outFactor = {colour.R, colour.G, colour.B};
			}
			else
			{
				LOG_UNSUPPORTED_MATERIAL_EXPRESSION(materialInterface, name);

				if(outTexture.index == 0)
				{
					GetDefaultTexture(materialInterface, propertyChain, outTexture);
				}
			}

			return expressionsHandled;
		};

		expressionDecomposer(0);
	}
}

void GeometrySource::DecomposeMaterialProperty(UMaterialInterface *materialInterface, EMaterialProperty propertyChain, avs::TextureAccessor &outTexture, avs::vec4 &outFactor)
{
	TArray<UMaterialExpression*> outExpressions;
	materialInterface->GetMaterial()->GetExpressionsInPropertyChain(propertyChain, outExpressions, nullptr);

	if(outExpressions.Num() != 0)
	{
		std::function<size_t(size_t)> expressionDecomposer = [&](size_t expressionIndex)
		{
			size_t expressionsHandled = 1;
			FString name = outExpressions[expressionIndex]->GetName();

			if(name.Contains("Multiply"))
			{
				expressionsHandled += expressionDecomposer(expressionIndex + expressionsHandled);
				expressionsHandled += expressionDecomposer(expressionIndex + expressionsHandled);
			}
			else if(name.Contains("TextureSample"))
			{
				expressionsHandled += DecomposeTextureSampleExpression(materialInterface, Cast<UMaterialExpressionTextureSample>(outExpressions[expressionIndex]), outTexture);
			}
			else if(name.Contains("Constant3Vector"))
			{
				FLinearColor colour = Cast<UMaterialExpressionConstant3Vector>(outExpressions[expressionIndex])->Constant;
				outFactor = {colour.R, colour.G, colour.B, colour.A};
			}
			else if(name.Contains("Constant4Vector"))
			{
				FLinearColor colour = Cast<UMaterialExpressionConstant4Vector>(outExpressions[expressionIndex])->Constant;
				outFactor = {colour.R, colour.G, colour.B, colour.A};
			}
			else if(name.Contains("VectorParameter"))
			{
				FLinearColor colour;
				///INFO: Just using the parameter's name won't work for layered materials.
				materialInterface->GetVectorParameterValue(outExpressions[expressionIndex]->GetParameterName(), colour);

				outFactor = {colour.R, colour.G, colour.B, colour.A};
			}
			else
			{
				LOG_UNSUPPORTED_MATERIAL_EXPRESSION(materialInterface, name);

				if(outTexture.index == 0)
				{
					GetDefaultTexture(materialInterface, propertyChain, outTexture);
				}
			}

			return expressionsHandled;
		};

		expressionDecomposer(0);
	}
}

size_t GeometrySource::DecomposeTextureSampleExpression(UMaterialInterface* materialInterface, UMaterialExpressionTextureSample* textureSample, avs::TextureAccessor& outTexture)
{
	size_t subExpressionsHandled = 0;
	outTexture = {AddTexture(textureSample->Texture), DUMMY_TEX_COORD};

	//Extract tiling data for this texture.
	if(textureSample->Coordinates.Expression)
	{
		//Name of the coordinate expression.
		FString coordExpName = textureSample->Coordinates.Expression->GetName();

		if(coordExpName.Contains("Multiply"))
		{
			UMaterialExpressionMultiply* mulExp = Cast<UMaterialExpressionMultiply>(textureSample->Coordinates.Expression);
			UMaterialExpression* inputA = mulExp->A.Expression, * inputB = mulExp->B.Expression;

			if(inputA && inputB)
			{
				FString inputAName = mulExp->A.Expression->GetName(), inputBName = mulExp->B.Expression->GetName();

				//Swap, so A is texture coordinate, if B is texture coordinate.
				if(inputBName.Contains("TextureCoordinate"))
				{
					std::swap(inputA, inputB);
					std::swap(inputAName, inputBName);
				}

				if(inputAName.Contains("TextureCoordinate"))
				{
					bool isBSupported = true;
					float scalarValue = 0;

					if(inputBName.Contains("Constant"))
					{
						scalarValue = Cast<UMaterialExpressionConstant>(inputB)->R;
					}
					else if(inputBName.Contains("ScalarParameter"))
					{
						///INFO: Just using the parameter's name won't work for layered materials.
						materialInterface->GetScalarParameterValue(inputB->GetParameterName(), scalarValue);
					}
					else
					{
						isBSupported = false;
						LOG_UNSUPPORTED_MATERIAL_EXPRESSION(materialInterface, inputBName)
					}

					if(isBSupported)
					{
						UMaterialExpressionTextureCoordinate* texCoordExp = Cast<UMaterialExpressionTextureCoordinate>(inputA);
						outTexture.tiling = {texCoordExp->UTiling * scalarValue, texCoordExp->VTiling * scalarValue};
					}
				}
				else
				{
					UE_LOG(LogRemotePlay, Warning, TEXT("Material <%s> contains multiply expression <%s> with missing inputs."), *materialInterface->GetName(), *coordExpName)
				}
			}

			subExpressionsHandled += (inputA ? 1 : 0) + (inputB ? 1 : 0); //Handled multiplication inputs.
		}
		else if(coordExpName.Contains("TextureCoordinate"))
		{
			UMaterialExpressionTextureCoordinate* texCoordExp = Cast<UMaterialExpressionTextureCoordinate>(textureSample->Coordinates.Expression);
			outTexture.tiling = {texCoordExp->UTiling, texCoordExp->VTiling};
		}
		else
		{
			LOG_UNSUPPORTED_MATERIAL_EXPRESSION(materialInterface, coordExpName)
		}

		++subExpressionsHandled; //Handled UV expression.
	}

	return subExpressionsHandled;
}