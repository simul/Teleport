#include "GeometrySource.h"
#include "StreamableGeometryComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Rendering/StaticMeshVertexBuffer.h"
#include "Rendering/PositionVertexBuffer.h"
#include "StaticMeshResources.h"

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

#include "RemotePlayMonitor.h"

#include <functional> //std::function

#if 0
#include <random>
std::default_random_engine generator;
std::uniform_int_distribution<int> distribution(1, 6);
int dice_roll = distribution(generator);
#endif

#define LOG_UNSUPPORTED_MATERIAL_EXPRESSION(materialInterface, name) UE_LOG(LogRemotePlay, Warning, TEXT("%s"), *("Decomposing <" + materialInterface->GetName() + ">: Unsupported expression with type name <" + name + ">"));
#define LOG_UNSUPPORTED_MATERIAL_CHAIN_LENGTH(materialInterface, length) UE_LOG(LogRemotePlay, Warning, TEXT("%s"), *("Decomposing <" + materialInterface->GetName() + ">: Unsupported property chain length of <" + length + ">"));

struct GeometrySource::Mesh
{
	~Mesh()
	{
	}
	UStaticMesh* StaticMesh;
	//unsigned long long SentFrame;
	bool Confirmed;
	std::vector<avs::PrimitiveArray> primitiveArrays;
	std::vector<avs::Attribute> attributes;
};

namespace
{
	const unsigned long long DUMMY_TEX_COORD = 0;
}

GeometrySource::GeometrySource()
	:Monitor(nullptr)
{
	basisCompressorParams.m_tex_type = basist::basis_texture_type::cBASISTexType2D;

	const uint32_t THREAD_AMOUNT = 16;
	basisCompressorParams.m_pJob_pool = new basisu::job_pool(THREAD_AMOUNT);
}

GeometrySource::~GeometrySource()
{
	clearData();

	delete basisCompressorParams.m_pJob_pool;
}

void GeometrySource::Initialize(ARemotePlayMonitor* monitor, UWorld* world)
{
	Monitor = monitor;

	if (Monitor)
	{
		if (Monitor->ResetCache)
		{
			clearData();
		}
	}
	//0 == No item. First generated UID will be 1.
	rootNodeUid = CreateNode(nullptr, 0, avs::NodeDataType::Scene,std::vector<avs::uid>());

	//Create the hand nodes, but only if we have not already done so.
	if(handUIDs.size() == 0)
	{
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
			avs::uid firstHandUID = AddNode(rootNodeUid, handMeshComponent);
			nodes[firstHandUID]->data_type = avs::NodeDataType::Hand;

			avs::uid secondHandUID = avs::GenerateUid();
			nodes[secondHandUID] = nodes[firstHandUID];

			handUIDs = {firstHandUID, secondHandUID};
		}
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

bool GeometrySource::InitMesh(Mesh *m, uint8 lodIndex) const
{
	if (m->StaticMesh->GetClass()->IsChildOf(USkeletalMesh::StaticClass()))
	{
		return false;
	}

	UStaticMesh *StaticMesh = Cast<UStaticMesh>(m->StaticMesh);
	auto &lods = StaticMesh->RenderData->LODResources;
	if (!lods.Num())
		return false;

	auto AddBuffer = [this](GeometrySource::Mesh *m, avs::uid b_uid, size_t num, size_t stride, const void *data)
	{
		// Data may already exist:
		if (geometryBuffers.find(b_uid) == geometryBuffers.end())
		{
			avs::GeometryBuffer& b = geometryBuffers[b_uid];
			b.byteLength = num * stride;
			b.data = (const uint8_t *)data;			// Remember, just a pointer: we don't own this data.
		}
	};
	auto AddBufferView = [this](GeometrySource::Mesh *m, avs::uid b_uid, avs::uid v_uid, size_t start_index, size_t num, size_t stride)
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
	std::vector<FVector2D>& uvData = processedUVs[texcoords_uid];
	avs::uid indices_uid = avs::GenerateUid();
	avs::uid positions_view_uid = avs::GenerateUid();
	avs::uid normals_view_uid = avs::GenerateUid();
	avs::uid tangents_view_uid = avs::GenerateUid();
	avs::uid texcoords_view_uid[8];
	size_t attributeCount = 2 + (vb.GetTangentData() ? 1 : 0) + (vb.GetTexCoordData() ? vb.GetNumTexCoords() : 0);

	uvData.reserve(vb.GetNumVertices()*vb.GetNumTexCoords());

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
		AddBuffer(m, positions_uid, pb.GetNumVertices(), stride, (const void*)p.data());
		size_t position_stride = pb.GetStride();
		// Offset is zero, because the sections are just lists of indices. 
		AddBufferView(m, positions_uid, positions_view_uid, 0, pb.GetNumVertices(), position_stride);
	}
	size_t tangent_stride = vb.GetTangentSize() / vb.GetNumVertices();
	// Normal:
	{
		size_t stride = vb.GetTangentSize() / vb.GetNumVertices();
		AddBuffer(m, normals_uid, vb.GetNumVertices(), stride, vb.GetTangentData());
		AddBufferView(m, normals_uid, normals_view_uid,  0, pb.GetNumVertices(), tangent_stride);
	}

	// Tangent:
	if (vb.GetTangentData())
	{
		size_t stride = vb.GetTangentSize() / vb.GetNumVertices();
		AddBuffer(m, tangents_uid, vb.GetNumVertices(), stride, vb.GetTangentData());
		AddBufferView(m, tangents_uid, tangents_view_uid, 0, pb.GetNumVertices(), tangent_stride);
	}
	// TexCoords:
	size_t texcoords_stride = sizeof(FVector2D);
	for (size_t j = 0; j < vb.GetNumTexCoords(); j++)
	{
		//bool IsFP32 = vb.GetUseFullPrecisionUVs(); //Not need vb.GetVertexUV() returns FP32 regardless. 

		for (uint32_t k = 0; k < vb.GetNumVertices(); k++)
			uvData.push_back(vb.GetVertexUV(k, j));
	}
	AddBuffer(m, texcoords_uid, vb.GetNumVertices()*vb.GetNumTexCoords(), texcoords_stride, uvData.data());
	for (size_t j = 0; j < vb.GetNumTexCoords(); j++)
	{
		//bool IsFP32 = vb.GetUseFullPrecisionUVs(); //Not need vb.GetVertexUV() returns FP32 regardless. 
		texcoords_view_uid[j] = avs::GenerateUid();
		AddBufferView(m, texcoords_uid, texcoords_view_uid[j], j*pb.GetNumVertices(), pb.GetNumVertices(), texcoords_stride);
	}
	FRawStaticIndexBuffer &ib = lod.IndexBuffer;
	FIndexArrayView arr = ib.GetArrayView();
	avs::Accessor::ComponentType componentType = ib.Is32Bit() ? avs::Accessor::ComponentType::UINT : avs::Accessor::ComponentType::USHORT;
	size_t istride = avs::GetComponentSize(componentType);
	AddBuffer(m, indices_uid, ib.GetNumIndices(), istride, (const void*)((uint64*)&arr)[0]);
	avs::uid indices_view_uid = avs::GenerateUid();
	AddBufferView(m, indices_uid, indices_view_uid, 0, ib.GetNumIndices(), istride);
	
	// Now create the views:
	size_t  num_elements = lod.Sections.Num();
	m->primitiveArrays.resize(num_elements);
	for (size_t i = 0; i < num_elements; i++)
	{
		auto &section = lod.Sections[i];
		auto &pa = m->primitiveArrays[i];
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
			a.count = vb.GetTangentSize();// same as pb???
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
		pa.indices_accessor = avs::GenerateUid();

		avs::Accessor &i_a = accessors[pa.indices_accessor];
		i_a.byteOffset = section.FirstIndex*istride;
		i_a.type = avs::Accessor::DataType::SCALAR;
		i_a.componentType = componentType;
		i_a.count = section.NumTriangles*3;// same as pb???
		i_a.bufferView = indices_view_uid ;

		// probably no default material in UE4?
		pa.material = 0;
		pa.primitiveMode = avs::PrimitiveMode::TRIANGLES;
	}
	return true;
}

void GeometrySource::UpdateNodeTransform(std::shared_ptr<avs::DataNode>& node, USceneComponent* component)
{
	if(component)
	{
		FTransform transform = component->GetComponentTransform();

		// convert offset from cm to metres.
		FVector t = transform.GetTranslation() * 0.01f;
		// We retain Unreal axes until sending to individual clients, which might have varying standards.
		FQuat r = transform.GetRotation();
		const FVector s = transform.GetScale3D();

		node->transform = {t.X, t.Y, t.Z, r.X, r.Y, r.Z, r.W, s.X, s.Y, s.Z};
	}
	else
	{
		UE_LOG(LogRemotePlay, Log, TEXT("UpdateNodeTransform used on node with nullptr component."))
	}
}

void GeometrySource::clearData()
{
	Meshes.Empty();
	accessors.clear();
	bufferViews.clear();
	geometryBuffers.clear();
	scaledPositionBuffers.clear();
	processedUVs.clear();

	nodes.clear();

	decomposedTextures.clear();
	decomposedMaterials.clear();
	decomposedNodes.clear();
	storedShadowMaps.clear();

	textures.clear();
	materials.clear();
	shadowMaps.clear();

	handUIDs.clear();
}

// By adding a m, we also add a pipe, including the InputMesh, which must be configured with the appropriate 
avs::uid GeometrySource::AddMesh(UStaticMesh *StaticMesh)
{
	avs::uid uid = avs::GenerateUid();
	TSharedPtr<Mesh> m(new Mesh);
	Meshes.Add(uid, m);
	m->StaticMesh = StaticMesh;
	m->Confirmed = false;
	PrepareMesh(*m);
	return uid;
}

avs::uid GeometrySource::AddStreamableMeshComponent(UMeshComponent *MeshComponent)
{
	if (MeshComponent->GetClass()->IsChildOf(USkeletalMeshComponent::StaticClass()))
	{
		UE_LOG(LogRemotePlay, Warning, TEXT("Skeletal meshes not supported yet"));
		return 0;
	}

	avs::uid mesh_uid=avs::uid(0);
	UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(MeshComponent);
	UStaticMesh *StaticMesh = StaticMeshComponent->GetStaticMesh();
	bool already_got_mesh = false;
	for (auto &i : Meshes)
	{
		if (i.Value->StaticMesh->GetClass()->IsChildOf(USkeletalMesh::StaticClass()))
		{
			continue;
		}
		if (StaticMesh == i.Value->StaticMesh)
		{
			already_got_mesh = true;
			mesh_uid = i.Key;
		}
	}
	if (!already_got_mesh)
	{
		mesh_uid = AddMesh(StaticMesh);
	}

	return mesh_uid;
}

avs::uid GeometrySource::AddNode(avs::uid parent_uid, USceneComponent* component, bool forceTransformUpdate)
{
	avs::uid node_uid;
	if (!component)
		return 0;
	FName levelUniqueNodeName = *FPaths::Combine(component->GetOutermost()->GetName(), component->GetOuter()->GetName(), component->GetName());
	std::map<FName, avs::uid>::iterator nodeIt = decomposedNodes.find(levelUniqueNodeName);

	if(nodeIt != decomposedNodes.end())
	{
		node_uid = nodeIt->second;

		if(forceTransformUpdate)
		{
			UpdateNodeTransform(nodes[node_uid], component);
		}
	}
	else
	{
		UMeshComponent* meshComponent = Cast<UMeshComponent>(component);
		ULightComponent* lightComponent = Cast<ULightComponent>(component);

		if(meshComponent)
		{
			std::shared_ptr<avs::DataNode> parent;
			getNode(parent_uid, parent);

			avs::uid mesh_uid = AddStreamableMeshComponent(meshComponent);
			// the material/s that this particular instance of the mesh has applied to its slots...
			TArray<UMaterialInterface*> mats = meshComponent->GetMaterials();

			std::vector<avs::uid> mat_uids;
			//Add material, and textures, for streaming to clients.
			int32 num_mats = mats.Num();
			for(int32 i = 0; i < num_mats; i++)
			{
				UMaterialInterface* materialInterface = mats[i];
				mat_uids.push_back(AddMaterial(materialInterface));
			}

			node_uid = CreateNode(component, mesh_uid, avs::NodeDataType::Mesh, mat_uids);
			decomposedNodes[levelUniqueNodeName] = node_uid;

			parent->childrenUids.push_back(node_uid);

			TArray<USceneComponent*> children;
			component->GetChildrenComponents(false, children);

			for(auto child : children)
			{
				if(child->GetClass()->IsChildOf(UMeshComponent::StaticClass()))
				{
					AddNode(node_uid, Cast<UMeshComponent>(child));
				}
			}
		}
		else if (lightComponent)
		{
			std::shared_ptr<avs::DataNode> parent;
			getNode(parent_uid, parent);

			avs::uid shadow_uid = AddShadowMap(lightComponent->StaticShadowDepthMap.Data);

			node_uid = CreateNode(component, shadow_uid, avs::NodeDataType::ShadowMap, {});
			decomposedNodes[levelUniqueNodeName] = node_uid;

			//This node is a Terminus. i.e. no children.
		}
		else
		{
			UE_LOG(LogRemotePlay, Warning, TEXT("Currently only UMeshComponents and ULightComponents are supported, but a component of type <%s> was passed to the GeometrySource."), *component->GetName())
				return 0;
		}
	}

	return node_uid;
}

avs::uid GeometrySource::CreateNode(USceneComponent* component, avs::uid data_uid, avs::NodeDataType data_type, const std::vector<avs::uid>& mat_uids)
{
	avs::uid uid = avs::GenerateUid();
	std::shared_ptr<avs::DataNode> node = std::make_shared<avs::DataNode>();
	UpdateNodeTransform(node, component);
	node->data_uid = data_uid;
	node->data_type = data_type;
	node->materials = mat_uids;
	nodes[uid] = node;
	return uid;
}

avs::uid GeometrySource::GetRootNodeUid()
{
	return rootNodeUid;
}

bool GeometrySource::GetRootNode(std::shared_ptr<avs::DataNode>& node)
{
	return getNode(rootNodeUid, node);
}

avs::uid GeometrySource::AddMaterial(UMaterialInterface *materialInterface)
{
	//Return 0 if we were passed a nullptr.
	if(!materialInterface) return 0;

	avs::uid mat_uid;

	//Try and locate the pointer in the list of processed materials.
	std::unordered_map<UMaterialInterface*, avs::uid>::iterator materialIt = decomposedMaterials.find(materialInterface);

	//Return the UID of the already processed material, if we have already processed the material.
	if(materialIt != decomposedMaterials.end())
	{
		mat_uid = materialIt->second;
	}
	//Store the material if we have yet to process it.
	else
	{
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
			TArray<UMaterialExpression *> outExpressions;
			materialInterface->GetMaterial()->GetExpressionsInPropertyChain(MP_WorldPositionOffset, outExpressions, nullptr);

			if(outExpressions.Num() != 0)
			{
				if(outExpressions[0]->GetName().Contains("MaterialFunctionCall"))
				{
					UMaterialExpressionMaterialFunctionCall *functionExp = Cast<UMaterialExpressionMaterialFunctionCall>(outExpressions[0]);
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
							simpleGrassWind.texUID = StoreTexture(Cast<UMaterialExpressionTextureBase>(functionExp->FunctionInputs[3].Input.Expression)->Texture);
						}

						newMaterial.extensions[avs::MaterialExtensionIdentifier::SIMPLE_GRASS_WIND] = std::make_unique<avs::SimpleGrassWindExtension>(simpleGrassWind);
					}
				}
			}
		}
		
		mat_uid = avs::GenerateUid();

		materials[mat_uid] = newMaterial;
		decomposedMaterials[materialInterface] = mat_uid;

		UE_CLOG(newMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index != newMaterial.occlusionTexture.index, LogRemotePlay, Warning, TEXT("Occlusion texture on material <%s> is not combined with metallic-roughness texture."), *materialInterface->GetName());
	}

	return mat_uid;
}

avs::uid GeometrySource::AddShadowMap(const FStaticShadowDepthMapData* shadowDepthMapData)
{
	//Check for nullptr
	if (!shadowDepthMapData)
		return 0;

	//Return pre-stored shadow_uid
	auto it = storedShadowMaps.find(shadowDepthMapData);
	if (it != storedShadowMaps.end())
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

	//Store in std::maps
	shadowMaps[shadow_uid] = shadowTexture;
	storedShadowMaps[shadowDepthMapData] = shadow_uid;
	
	return shadow_uid;
}

void GeometrySource::Tick()
{

}

void GeometrySource::PrepareMesh(Mesh &m)
{
	// We will pre-encode the mesh to prepare it for streaming.
	if (m.StaticMesh->GetClass()->IsChildOf(UStaticMesh::StaticClass()))
	{
		UStaticMesh* StaticMesh = m.StaticMesh;
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

avs::uid GeometrySource::StoreTexture(UTexture * texture)
{
	avs::uid texture_uid;
	auto it = decomposedTextures.find(texture);

	//Retrieve the uid if we have already processed this texture.
	if(it != decomposedTextures.end())
	{
		texture_uid = it->second;
	}
	//Otherwise, store it.
	else
	{
		texture_uid = avs::GenerateUid();

		std::string textureName = TCHAR_TO_ANSI(*texture->GetName());

		//Assuming the first running platform is the desired running platform.
		FTexture2DMipMap baseMip = texture->GetRunningPlatformData()[0]->Mips[0];
		FTextureSource& textureSource = texture->Source;
		ETextureSourceFormat unrealFormat = textureSource.GetFormat();

		uint32_t width = baseMip.SizeX;
		uint32_t height = baseMip.SizeY;
		uint32_t depth = baseMip.SizeZ; ///!!! Is this actually where Unreal stores its depth information for a texture? !!!
		uint32_t bytesPerPixel = textureSource.GetBytesPerPixel();
		uint32_t arrayCount = textureSource.GetNumSlices(); ///!!! Is this actually the array count? !!!
		uint32_t mipCount = textureSource.GetNumMips();
		avs::TextureFormat format;

		UE_CLOG(bytesPerPixel != 4, LogRemotePlay, Warning, TEXT("Texture \"%s\" has bytes per pixel of %d!"), *texture->GetName(), bytesPerPixel);

		std::size_t texSize = width * height * bytesPerPixel;

		switch(unrealFormat)
		{
			case ETextureSourceFormat::TSF_Invalid:
				format = avs::TextureFormat::INVALID;
				break;
			case ETextureSourceFormat::TSF_G8:
				format = avs::TextureFormat::G8;
				break;
			case ETextureSourceFormat::TSF_BGRA8:
				format = avs::TextureFormat::BGRA8;
				break;
			case ETextureSourceFormat::TSF_BGRE8:
				format = avs::TextureFormat::BGRE8;
				break;
			case ETextureSourceFormat::TSF_RGBA16:
				format = avs::TextureFormat::RGBA16;
				break;
			case ETextureSourceFormat::TSF_RGBA16F:
				format = avs::TextureFormat::RGBA16F;
				break;
			case ETextureSourceFormat::TSF_RGBA8:
				format = avs::TextureFormat::RGBA8;
				break;
			case ETextureSourceFormat::TSF_RGBE8:
				format = avs::TextureFormat::INVALID;
				break;
			case ETextureSourceFormat::TSF_MAX:
			default:
				format = avs::TextureFormat::INVALID;
				UE_LOG(LogRemotePlay, Warning, TEXT("Invalid texture format"));
				break;
		}		

		avs::TextureCompression compression = avs::TextureCompression::UNCOMPRESSED;
		
		uint32_t dataSize=0;
		unsigned char* data = nullptr;
		//Compress the texture with Basis Universal if the flag is set, and bytes per pixel is equal to 4.
		if(Monitor->UseCompressedTextures && bytesPerPixel == 4)
		{
			compression = avs::TextureCompression::BASIS_COMPRESSED;
			bool validBasisFileExists = false;

			FString GameSavedDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());

			//Create a unique name based on the filepath.
			FString uniqueName = FPaths::ConvertRelativePathToFull(texture->AssetImportData->SourceData.SourceFiles[0].RelativeFilename);
			uniqueName = uniqueName.Replace(TEXT("/"), TEXT("#")); //Replaces slashes with hashes.
			uniqueName = uniqueName.RightChop(2); //Remove drive.
			uniqueName = uniqueName.Right(255); //Restrict name length.

			FString basisFilePath = FPaths::Combine(GameSavedDir, uniqueName + FString(".basis"));
			
			FFileManagerGeneric fileManager;
			if(fileManager.FileExists(*basisFilePath))
			{
				FDateTime basisLastModified = fileManager.GetTimeStamp(*basisFilePath);
				FDateTime textureLastModified = texture->AssetImportData->SourceData.SourceFiles[0].Timestamp;

				//The file is valid if the basis file is younger than the texture file.
				validBasisFileExists = basisLastModified > textureLastModified;
			}

			//Read from disk if the file exists.
			if(validBasisFileExists)
			{
				FArchive *reader = fileManager.CreateFileReader(*basisFilePath);
				
				dataSize = reader->TotalSize();
				data = new unsigned char[dataSize];
				reader->Serialize(data, dataSize);

				reader->Close();
				delete reader;
			}
			//Otherwise, compress the file.
			else
			{
				TArray<uint8> mipData;
				textureSource.GetMipData(mipData, 0);

				basisu::image image(width, height);
				basisu::color_rgba_vec& imageData = image.get_pixels();
				memcpy(imageData.data(), mipData.GetData(), texSize);

				basisCompressorParams.m_source_images.clear();
				basisCompressorParams.m_source_images.push_back(image);

				basisCompressorParams.m_write_output_basis_files = true;
				basisCompressorParams.m_out_filename = TCHAR_TO_ANSI(*basisFilePath);

				basisCompressorParams.m_quality_level = Monitor->QualityLevel;
				basisCompressorParams.m_compression_level = Monitor->CompressionLevel;

				basisCompressorParams.m_mip_gen = true;
				basisCompressorParams.m_mip_smallest_dimension = 4; //Appears to be the smallest texture size that SimulFX handles.

				basisu::basis_compressor basisCompressor;

				if(basisCompressor.init(basisCompressorParams))
				{
					basisu::basis_compressor::error_code result = basisCompressor.process();

					if(result == basisu::basis_compressor::error_code::cECSuccess)
					{
						basisu::uint8_vec basisTex = basisCompressor.get_output_basis_file();

						dataSize = basisCompressor.get_basis_file_size();
						data = new unsigned char[dataSize];
						memcpy(data, basisTex.data(), dataSize);
					}
				}
			}
		}

		//We send over the uncompressed texture data, if we can't get the compressed data.
		if(!data)
		{
			TArray<uint8> mipData;
			textureSource.GetMipData(mipData, 0);

			dataSize = texSize;
			data = new unsigned char[dataSize];
			memcpy(data, mipData.GetData(), dataSize);

			UE_CLOG(dataSize > 1048576, LogRemotePlay, Warning, TEXT("Texture \"%s\" was stored UNCOMPRESSED with a data size larger than 1MB! Size: %dB(%.2fMB)"), *texture->GetName(), dataSize, dataSize / 1048576.0f)
		}

		//We're using a single sampler for now.
		avs::uid sampler_uid = 0;

		textures[texture_uid] = {textureName, width, height, depth, bytesPerPixel, arrayCount, mipCount, format, compression, dataSize, data, sampler_uid};
		decomposedTextures[texture] = texture_uid;
	}

	return texture_uid;
}

void GeometrySource::GetDefaultTexture(UMaterialInterface *materialInterface, EMaterialProperty propertyChain, avs::TextureAccessor &outTexture)
{
	TArray<UTexture*> outTextures;
	materialInterface->GetTexturesInPropertyChain(propertyChain, outTextures, nullptr, nullptr);

	UTexture *texture = outTextures.Num() ? outTextures[0] : nullptr;
	
	if(texture)
	{
		outTexture = {StoreTexture(texture), DUMMY_TEX_COORD};
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
	outTexture = {StoreTexture(textureSample->Texture), DUMMY_TEX_COORD};

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

std::vector<avs::uid> GeometrySource::getNodeUIDs() const
{
	std::vector<avs::uid> nodeUIDs(nodes.size());

	size_t i = 0;
	for(const auto &it : nodes)
	{
		nodeUIDs[i++] = it.first;
	}

	return nodeUIDs;
}

bool GeometrySource::getNode(avs::uid node_uid, std::shared_ptr<avs::DataNode> & outNode) const
{
	//Assuming an incorrect node uid should not happen, or at least not frequently.
	try
	{
		outNode = nodes.at(node_uid);

		return true;
	}
	catch(std::out_of_range oor)
	{
		UE_LOG(LogRemotePlay, Warning, TEXT("Failed to find node with UID: %d"), node_uid)
		return false;
	}
}

std::map<avs::uid, std::shared_ptr<avs::DataNode>>& GeometrySource::getNodes() const
{
	return nodes;
}

size_t GeometrySource::getMeshCount() const
{
	return Meshes.Num();
}

avs::uid GeometrySource::getMeshUid(size_t index) const
{
	TArray<avs::uid> MeshUids;
	Meshes.GenerateKeyArray(MeshUids);
	return MeshUids[index];
}

size_t GeometrySource::getMeshPrimitiveArrayCount(avs::uid mesh_uid) const
{
	auto &mesh = Meshes[mesh_uid];
	if (mesh->StaticMesh->GetClass()->IsChildOf(UStaticMesh::StaticClass()))
	{
		if (!mesh->StaticMesh->RenderData)
			return 0;
		auto &lods = mesh->StaticMesh->RenderData->LODResources;
		if (!lods.Num())
			return 0;
		return lods[0].Sections.Num();
	}
	return 0;
}

bool GeometrySource::getMeshPrimitiveArray(avs::uid mesh_uid, size_t array_index, avs::PrimitiveArray & primitiveArray) const
{
	GeometrySource::Mesh *mesh = Meshes[mesh_uid].Get();
	bool result = true;
	if (!mesh->primitiveArrays.size())
	{
		result = InitMesh(mesh, 0);
	}
	primitiveArray = mesh->primitiveArrays[array_index];
	return result;
}

bool GeometrySource::getAccessor(avs::uid accessor_uid, avs::Accessor & accessor) const
{
	auto it = accessors.find(accessor_uid);
	if (it == accessors.end())
	{
		UE_LOG(LogRemotePlay, Error, TEXT("Accessor not found!"));
		return false;
	}
	accessor = accessors[accessor_uid];
	return true;
}

bool GeometrySource::getBufferView(avs::uid buffer_view_uid, avs::BufferView & bufferView) const
{
	auto it = bufferViews.find(buffer_view_uid);
	if (it == bufferViews.end())
	{
		UE_LOG(LogRemotePlay, Error, TEXT("Buffer View not found!"));
		return false;
	}
	bufferView = bufferViews[buffer_view_uid];
	return true;
}

bool GeometrySource::getBuffer(avs::uid buffer_uid, avs::GeometryBuffer & buffer) const
{
	auto it = geometryBuffers.find(buffer_uid);
	if (it == geometryBuffers.end())
	{
		UE_LOG(LogRemotePlay, Error, TEXT("Buffer not found!"));
		return false;
	}
	buffer = geometryBuffers[buffer_uid];
	return true;
}

std::vector<avs::uid> GeometrySource::getTextureUIDs() const
{
	std::vector<avs::uid> textureUIDs(textures.size());

	size_t i = 0;
	for(const auto &it : textures)
	{
		textureUIDs[i++] = it.first;
	}

	return textureUIDs;
}

bool GeometrySource::getTexture(avs::uid texture_uid, avs::Texture & outTexture) const
{
	//Assuming an incorrect texture uid should not happen, or at least not frequently.
	try
	{
		outTexture = textures.at(texture_uid);

		return true;
	}
	catch(std::out_of_range oor)
	{
		UE_LOG(LogRemotePlay, Warning, TEXT("Failed to find texture with UID: %d"), texture_uid)
		return false;
	}
}

std::vector<avs::uid> GeometrySource::getMaterialUIDs() const
{
	std::vector<avs::uid> materialUIDs(materials.size());

	size_t i = 0;
	for(const auto &it : materials)
	{
		materialUIDs[i++] = it.first;
	}

	return materialUIDs;
}

bool GeometrySource::getMaterial(avs::uid material_uid, avs::Material & outMaterial) const
{
	//Assuming an incorrect material uid should not happen, or at least not frequently.
	try
	{
		outMaterial = materials.at(material_uid);

		return true;
	}
	catch(std::out_of_range oor)
	{
		UE_LOG(LogRemotePlay, Warning, TEXT("Failed to find material with UID: %d"), material_uid)
		return false;
	}
}

std::vector<avs::uid> GeometrySource::getShadowMapUIDs() const
{
	std::vector<avs::uid> shadowMapUIDs(shadowMaps.size());

	size_t i = 0;
	for (const auto& it : shadowMaps)
	{
		shadowMapUIDs[i++] = it.first;
	}

	return shadowMapUIDs;
}

bool GeometrySource::getShadowMap(avs::uid shadow_uid, avs::Texture& outShadowMap) const
{
	//Assuming an incorrect texture uid should not happen, or at least not frequently.
	try
	{
		outShadowMap = shadowMaps.at(shadow_uid);

		return true;
	}
	catch (std::out_of_range oor)
	{
		UE_LOG(LogRemotePlay, Warning, TEXT("Failed to find shadow map with UID: %d"), shadow_uid)
			return false;
	}
}
