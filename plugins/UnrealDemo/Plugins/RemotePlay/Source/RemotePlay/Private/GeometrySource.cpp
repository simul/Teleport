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
	UMeshComponent* MeshComponent;
	unsigned long long SentFrame;
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

void GeometrySource::Initialize(class ARemotePlayMonitor *monitor)
{
	rootNodeUid = CreateNode(FTransform::Identity, -1, avs::NodeDataType::Scene,std::vector<avs::uid>());

	Monitor = monitor;
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
	if (m->MeshComponent->GetClass()->IsChildOf(USkeletalMeshComponent::StaticClass()))
	{
		return false;
	}

	UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(m->MeshComponent);
	UStaticMesh *StaticMesh = StaticMeshComponent->GetStaticMesh();
	auto &lods = StaticMesh->RenderData->LODResources;
	if (!lods.Num())
		return false;

	auto &lod = lods[lodIndex];
	m->primitiveArrays.resize(lod.Sections.Num());
	for (size_t i = 0; i < m->primitiveArrays.size(); i++)
	{
		auto &section = lod.Sections[i];
		FPositionVertexBuffer &pb = lod.VertexBuffers.PositionVertexBuffer;
		FStaticMeshVertexBuffer &vb = lod.VertexBuffers.StaticMeshVertexBuffer;
		auto &pa = m->primitiveArrays[i];
		pa.attributeCount = 2 + (vb.GetTangentData() ? 1 : 0) + (vb.GetTexCoordData() ? vb.GetNumTexCoords() : 0);
		pa.attributes = new avs::Attribute[pa.attributeCount];
		auto AddBufferAndView = [this](GeometrySource::Mesh *m, avs::uid v_uid, avs::uid b_uid, size_t num, size_t stride, const void *data)
		{
			avs::BufferView &bv = bufferViews[v_uid];
			bv.byteOffset = 0;
			bv.byteLength = num * stride;
			bv.byteStride = stride;
			bv.buffer = b_uid;
			avs::GeometryBuffer& b = geometryBuffers[bv.buffer];
			b.byteLength = bv.byteLength;
			
			b.data = (const uint8_t *)data;			// Remember, just a pointer: we don't own this data.
			
		};
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
			a.bufferView = avs::GenerateUid();
			avs::uid bufferUid = avs::GenerateUid();
		
			std::vector<avs::vec3> &p = scaledPositionBuffers[bufferUid];
			p.resize(a.count);
			const avs::vec3 *orig =(const avs::vec3 *) pb.GetVertexData();
			for (size_t j = 0; j < a.count; j++)
			{
				p[j].x	 =orig[j].x *0.01f;
				p[j].y	 =orig[j].y *0.01f;
				p[j].z	 =orig[j].z *0.01f;
			}
			AddBufferAndView(m, a.bufferView, bufferUid, pb.GetNumVertices(), pb.GetStride(), (const void*)p.data());
		}
		// Normal:
		{
			avs::Attribute &attr = pa.attributes[idx++];
			attr.accessor = avs::GenerateUid();
			attr.semantic = avs::AttributeSemantic::NORMAL;
			avs::Accessor &a = accessors[attr.accessor];
			a.byteOffset = 0;
			a.type = avs::Accessor::DataType::VEC3;
			a.componentType = avs::Accessor::ComponentType::FLOAT;
			a.count = vb.GetNumVertices();// same as pb???
			a.bufferView = avs::GenerateUid();
			AddBufferAndView(m, a.bufferView, avs::GenerateUid(), vb.GetNumVertices(), vb.GetTangentSize() / vb.GetNumVertices(), vb.GetTangentData());
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
			a.bufferView = avs::GenerateUid();
			AddBufferAndView(m, a.bufferView, avs::GenerateUid(),vb.GetNumVertices(), vb.GetTangentSize() / vb.GetNumVertices(), vb.GetTangentData());
		}
		// TexCoords:
		for (size_t j = 0; j < vb.GetNumTexCoords(); j++)
		{
			avs::Attribute &attr = pa.attributes[idx++];
			attr.accessor = avs::GenerateUid();
			attr.semantic = j == 0 ? avs::AttributeSemantic::TEXCOORD_0 : avs::AttributeSemantic::TEXCOORD_1;
			avs::Accessor &a = accessors[attr.accessor];
			a.byteOffset = 0;
			a.type = avs::Accessor::DataType::VEC2;
			a.componentType = avs::Accessor::ComponentType::FLOAT;
			a.count = vb.GetNumVertices();// same as pb???
			a.bufferView = avs::GenerateUid();
			avs::uid bufferUid = avs::GenerateUid();
			
			//bool IsFP32 = vb.GetUseFullPrecisionUVs(); //Not need vb.GetVertexUV() returns FP32 regardless. 
			std::vector<FVector2D>& uvData = processedUVs[bufferUid];
			uvData.reserve(a.count);
			for (uint32_t k = 0; k < vb.GetNumVertices(); k++)
				uvData.push_back(vb.GetVertexUV(k, j));

			AddBufferAndView(m, a.bufferView, bufferUid, vb.GetNumVertices(), sizeof(FVector2D), uvData.data());
		}
		// Indices:
		pa.indices_accessor = avs::GenerateUid();

		FRawStaticIndexBuffer &ib = lod.IndexBuffer;
		avs::Accessor &i_a = accessors[pa.indices_accessor];
		i_a.byteOffset = 0;
		i_a.type = avs::Accessor::DataType::SCALAR;
		i_a.componentType = ib.Is32Bit() ? avs::Accessor::ComponentType::UINT : avs::Accessor::ComponentType::USHORT;
		i_a.count = ib.GetNumIndices();// same as pb???
		i_a.bufferView = avs::GenerateUid();
		FIndexArrayView arr = ib.GetArrayView();
		AddBufferAndView(m, i_a.bufferView, avs::GenerateUid(), ib.GetNumIndices(), avs::GetComponentSize(i_a.componentType), (const void*)((uint64*)&arr)[0]);

		// probably no default material in UE4?
		pa.material = 0;
		pa.primitiveMode = avs::PrimitiveMode::TRIANGLES;
	}
	return true;
}

void GeometrySource::clearData()
{
	Meshes.Empty();
	accessors.clear();
	bufferViews.clear();
	geometryBuffers.clear();

	nodes.clear();

	processedTextures.clear();
	processedMaterials.clear();

	textures.clear();
	materials.clear();
}

// By adding a m, we also add a pipe, including the InputMesh, which must be configured with the appropriate 
avs::uid GeometrySource::AddMesh(UMeshComponent *MeshComponent)
{
	avs::uid uid = avs::GenerateUid();
	TSharedPtr<Mesh> m(new Mesh);
	Meshes.Add(uid, m);
	m->MeshComponent = MeshComponent;
	m->SentFrame = (unsigned long long)0;
	m->Confirmed = false;
	PrepareMesh(*m);
	return uid;
}

avs::uid GeometrySource::AddStreamableMeshComponent(UMeshComponent *MeshComponent)
{
	if (MeshComponent->GetClass()->IsChildOf(USkeletalMeshComponent::StaticClass()))
	{
		UE_LOG(LogRemotePlay, Warning, TEXT("Skeletal meshes not supported yet"));
		return -1;
	}

	avs::uid mesh_uid=avs::uid(0);
	UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(MeshComponent);
	UStaticMesh *StaticMesh = StaticMeshComponent->GetStaticMesh();
	bool already_got_mesh = false;
	for (auto &i : Meshes)
	{
		if (i.Value->MeshComponent->GetClass()->IsChildOf(USkeletalMeshComponent::StaticClass()))
		{
			continue;
		}
		UStaticMeshComponent* c = Cast<UStaticMeshComponent>(i.Value->MeshComponent);
		if (c->GetStaticMesh() == StaticMeshComponent->GetStaticMesh())
		{
			already_got_mesh = true;
			mesh_uid = i.Key;
		}
	}
	if (!already_got_mesh)
	{
		mesh_uid = AddMesh(MeshComponent);
	}

	return mesh_uid;
}

avs::uid GeometrySource::CreateNode(const FTransform& transform, avs::uid data_uid, avs::NodeDataType data_type, const std::vector<avs::uid> &mat_uids)
{
	avs::uid uid = avs::GenerateUid();
	auto node = std::make_shared<avs::DataNode>();
	// convert offset from cm to metres.
	FVector t = transform.GetTranslation()*0.01f;
	// We retain Unreal axes until sending to individual clients, which might have varying standards.
	FQuat r = transform.GetRotation();
	const FVector s = transform.GetScale3D();
	node->transform = { t.X, t.Y, t.Z, r.X, r.Y, r.Z, r.W, s.X, s.Y, s.Z };
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
	std::unordered_map<UMaterialInterface*, avs::uid>::iterator materialIt = processedMaterials.find(materialInterface);

	//Return the UID of the already processed material, if we have already processed the material.
	if(materialIt != processedMaterials.end())
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

		mat_uid = avs::GenerateUid();

		materials[mat_uid] = newMaterial;
		processedMaterials[materialInterface] = mat_uid;
	}

	return mat_uid;
}

void GeometrySource::Tick()
{

}

void GeometrySource::PrepareMesh(Mesh &m)
{
	// We will pre-encode the mesh to prepare it for streaming.
	if (m.MeshComponent->GetClass()->IsChildOf(UStaticMeshComponent::StaticClass()))
	{
		UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(m.MeshComponent);
		UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh();
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
	auto it = processedTextures.find(texture);

	//Retrieve the uid if we have already processed this texture.
	if(it != processedTextures.end())
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
		FTextureSource &textureSource = texture->Source;
		ETextureSourceFormat unrealFormat = textureSource.GetFormat();

		uint32_t width = baseMip.SizeX;
		uint32_t height = baseMip.SizeY;
		uint32_t depth = baseMip.SizeZ; ///!!! Is this actually where Unreal stores its depth information for a texture? !!!
		uint32_t bytesPerPixel = textureSource.GetBytesPerPixel();
		uint32_t arrayCount = textureSource.GetNumSlices(); ///!!! Is this actually the array count? !!!
		uint32_t mipCount = textureSource.GetNumMips();
		avs::TextureFormat format;

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

		TArray<uint8> mipData;
		textureSource.GetMipData(mipData, 0);		
		
		uint32_t dataSize=0;
		unsigned char* data = nullptr;

		//Compress the texture with Basis Universal if the flag is set.
		if(Monitor->UseCompressedTextures)
		{
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
				basisu::image image(width, height);
				basisu::color_rgba_vec& imageData = image.get_pixels();
				memcpy(imageData.data(), mipData.GetData(), texSize);

				basisCompressorParams.m_source_images.clear();
				basisCompressorParams.m_source_images.push_back(image);

				basisCompressorParams.m_write_output_basis_files = true;
				basisCompressorParams.m_out_filename = TCHAR_TO_ANSI(*basisFilePath);

				basisCompressorParams.m_quality_level = Monitor->QualityLevel;
				basisCompressorParams.m_compression_level = Monitor->CompressionLevel;

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
			dataSize = texSize;
			data = new unsigned char[dataSize];
			memcpy(data, mipData.GetData(), dataSize);
		}

		//We're using a single sampler for now.
		avs::uid sampler_uid = 0;

		textures[texture_uid] = {textureName, width, height, depth, bytesPerPixel, arrayCount, mipCount, format, dataSize, data, sampler_uid};
		processedTextures[texture] = texture_uid;
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

	std::function<void(size_t)> handleExpression = [&](size_t expressionIndex)
	{
		FString name = outExpressions[expressionIndex]->GetName();

		if(name.Contains("TextureSample"))
		{
			outTexture = {StoreTexture(Cast<UMaterialExpressionTextureBase>(outExpressions[expressionIndex])->Texture), DUMMY_TEX_COORD};
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
	};

	switch(outExpressions.Num())
	{
		case 0:
			//There is no property chain, so everything should be left as default.
			break;
		case 1:
			handleExpression(0);

			break;
		case 3:
		{
			FString name = outExpressions[0]->GetName();

			if(name.Contains("Multiply"))
			{
				handleExpression(1);
				handleExpression(2);
			}
			else
			{
				LOG_UNSUPPORTED_MATERIAL_EXPRESSION(materialInterface, name);
				GetDefaultTexture(materialInterface, propertyChain, outTexture);
			}
		}

			break;
		default:
			LOG_UNSUPPORTED_MATERIAL_CHAIN_LENGTH(materialInterface, FString::FromInt(outExpressions.Num()));
			GetDefaultTexture(materialInterface, propertyChain, outTexture);

			break;
	}
}

void GeometrySource::DecomposeMaterialProperty(UMaterialInterface *materialInterface, EMaterialProperty propertyChain, avs::TextureAccessor &outTexture, avs::vec3 &outFactor)
{
	TArray<UMaterialExpression *> outExpressions;
	materialInterface->GetMaterial()->GetExpressionsInPropertyChain(propertyChain, outExpressions, nullptr);

	std::function<void(size_t)> handleExpression = [&](size_t expressionIndex)
	{
		FString name = outExpressions[expressionIndex]->GetName();

		if(name.Contains("TextureSample"))
		{
			outTexture = {StoreTexture(Cast<UMaterialExpressionTextureBase>(outExpressions[expressionIndex])->Texture), DUMMY_TEX_COORD};
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
	};

	switch(outExpressions.Num())
	{
		case 0:
			//There is no property chain, so everything should be left as default.
			break;
		case 1:
		{
			handleExpression(0);
		}

		break;
		case 3:
		{
			FString name = outExpressions[0]->GetName();

			if(name.Contains("Multiply"))
			{
				handleExpression(1);
				handleExpression(2);
			}
			else
			{
				LOG_UNSUPPORTED_MATERIAL_EXPRESSION(materialInterface, name);
				GetDefaultTexture(materialInterface, propertyChain, outTexture);
			}
		}

		break;
		default:
			LOG_UNSUPPORTED_MATERIAL_CHAIN_LENGTH(materialInterface, FString::FromInt(outExpressions.Num()));
			GetDefaultTexture(materialInterface, propertyChain, outTexture);

		break;
	}
}

void GeometrySource::DecomposeMaterialProperty(UMaterialInterface *materialInterface, EMaterialProperty propertyChain, avs::TextureAccessor &outTexture, avs::vec4 &outFactor)
{
	TArray<UMaterialExpression*> outExpressions;
	materialInterface->GetMaterial()->GetExpressionsInPropertyChain(propertyChain, outExpressions, nullptr);

	std::function<void(size_t)> handleExpression = [&](size_t expressionIndex)
	{
		FString name = outExpressions[expressionIndex]->GetName();

		if(name.Contains("TextureSample"))
		{
			outTexture = {StoreTexture(Cast<UMaterialExpressionTextureBase>(outExpressions[expressionIndex])->Texture), DUMMY_TEX_COORD};
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
	};

	switch(outExpressions.Num())
	{
		case 0:
			//There is no property chain, so everything should be left as default.
			break;
		case 1:
			handleExpression(0);

			break;
		case 3:
		{
			FString name = outExpressions[0]->GetName();

			if(name.Contains("Multiply"))
			{
				handleExpression(1);
				handleExpression(2);
			}
			else
			{
				LOG_UNSUPPORTED_MATERIAL_EXPRESSION(materialInterface, name);
				GetDefaultTexture(materialInterface, propertyChain, outTexture);
			}
		}

			break;
		default:
			LOG_UNSUPPORTED_MATERIAL_CHAIN_LENGTH(materialInterface, FString::FromInt(outExpressions.Num()));
			GetDefaultTexture(materialInterface, propertyChain, outTexture);

			break;
	}
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
	if (mesh->MeshComponent->GetClass()->IsChildOf(UStaticMeshComponent::StaticClass()))
	{
		UStaticMeshComponent* staticMeshComponent = Cast<UStaticMeshComponent>(mesh->MeshComponent);
		UStaticMesh* staticMesh = staticMeshComponent->GetStaticMesh();
		if (!staticMesh->RenderData)
			return 0;
		auto &lods = staticMesh->RenderData->LODResources;
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
		return false;
	}
}
