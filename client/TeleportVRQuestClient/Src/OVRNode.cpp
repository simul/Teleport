#include "OVRNode.h"

#include "OVR_LogUtils.h"

#include "libavstream/common.hpp"

#include "crossplatform/Material.h"
#include "crossplatform/Mesh.h"

#include "GlobalGraphicsResources.h"
#include "SCR_Class_GL_Impl/GL_Effect.h"
#include "SCR_Class_GL_Impl/GL_IndexBuffer.h"
#include "SCR_Class_GL_Impl/GL_Texture.h"
#include "SCR_Class_GL_Impl/GL_UniformBuffer.h"
#include "SCR_Class_GL_Impl/GL_VertexBuffer.h"

void OVRNode::SetMesh(std::shared_ptr<scr::Mesh> mesh)
{
	Node::SetMesh(mesh);

	//Recreate surfaces for new mesh.
	RefreshOVRSurfaces();
}

void OVRNode::SetSkin(std::shared_ptr<scr::Skin> skin)
{
	Node::SetSkin(skin);

	//Recreate surfaces for new skin.
	RefreshOVRSurfaces();
}

void OVRNode::SetMaterial(size_t index, std::shared_ptr<scr::Material> material)
{
	Node::SetMaterial(index, material);

	//Surface may not exist as we don't have a mesh, or the index is in an invalid range.
	if(index >= ovrSurfaceDefs.size() || index < 0)
	{
		return;
	}

	ovrSurfaceDefs[index] = CreateOVRSurface(index, material);
}

void OVRNode::SetMaterialListSize(size_t size)
{
	Node::SetMaterialListSize(size);

	//We can't create surfaces without a mesh, so we shouldn't extend the surface list.
	std::shared_ptr<scr::Mesh> mesh = GetMesh();
	if(!mesh)
	{
		return;
	}

	size_t old_size = ovrSurfaceDefs.size();
	ovrSurfaceDefs.resize(size);
	if(old_size < size)
	{
		GlobalGraphicsResources& globalGraphicsResources = GlobalGraphicsResources::GetInstance();

		//We don't know have the information on the material yet, so we use placeholder OVR surfaces.
		for(size_t i = old_size; i < size; i++)
		{
			ovrSurfaceDefs[i] = CreateOVRSurface(i, globalGraphicsResources.renderPlatform.placeholderMaterial);
		}
	}
}

void OVRNode::SetMaterialList(std::vector<std::shared_ptr<scr::Material>>& materials)
{
	Node::SetMaterialList(materials);

	//Recreate surfaces for new material list.
	RefreshOVRSurfaces();
}

std::string OVRNode::GetCompleteEffectPassName(const char *effectPassName)
{
	return std::string(GetSkin() ? "Animated_" : "Static_") + effectPassName;
}

OVRFW::GlProgram* OVRNode::GetEffectPass(const char* effectPassName)
{
	std::string completePassName = GetCompleteEffectPassName(effectPassName);
	return GlobalGraphicsResources::GetInstance().defaultPBREffect.GetGlPlatform(completePassName.c_str());
}

void OVRNode::ChangeEffectPass(const char* effectPassName)
{
	OVRFW::GlProgram* effectPass = GetEffectPass(effectPassName);

	if(effectPass)
	{
		for(auto &surface : ovrSurfaceDefs)
		{
			surface.graphicsCommand.Program = *effectPass;
		}
	}
	else
	{
		OVR_ERROR("Invalid effect pass name! %s", effectPassName);
		OVR_ASSERT(false);
	}
}

OVRFW::ovrSurfaceDef OVRNode::CreateOVRSurface(size_t materialIndex, std::shared_ptr<scr::Material> material)
{
	GlobalGraphicsResources& globalGraphicsResources = GlobalGraphicsResources::GetInstance();
	OVRFW::GlProgram* effectPass = GetEffectPass(globalGraphicsResources.effectPassName);

	OVRFW::ovrSurfaceDef ovr_surface_def;
	ovr_surface_def.graphicsCommand.Program = *effectPass;

	if(material == nullptr)
	{
		OVR_WARN("Failed to create OVR surface! Null material passed to CreateOVRSurface(...)!");
		return ovr_surface_def;
	}

	std::shared_ptr<scr::Mesh> mesh = GetMesh();
	if(!mesh)
	{
		OVR_WARN("Failed to create OVR surface! OVRNode has no mesh!");
		return ovr_surface_def;
	}

	const scr::Mesh::MeshCreateInfo &meshCI = mesh->GetMeshCreateInfo();
	if(materialIndex >= meshCI.vb.size() || materialIndex >= meshCI.ib.size())
	{
		OVR_LOG("Failed to create OVR surface!\nMaterial index %zu greater than amount of mesh buffers: %zu Vertex | %zu Index", materialIndex, meshCI.vb.size(), meshCI.ib.size());
		return ovr_surface_def;
	}

	///MESH

	std::shared_ptr<scc::GL_VertexBuffer> gl_vb = std::dynamic_pointer_cast<scc::GL_VertexBuffer>(meshCI.vb[materialIndex]);
	std::shared_ptr<scc::GL_IndexBuffer> gl_ib = std::dynamic_pointer_cast<scc::GL_IndexBuffer>(meshCI.ib[materialIndex]);

	if(!gl_vb)
	{
		OVR_LOG("Failed to create OVR surface!\nNo vertex buffer to create OVR surface for material: %zu", materialIndex);
		return ovr_surface_def;
	}

	if(!gl_ib)
	{
		OVR_LOG("Failed to create OVR surface!\nNo index buffer to create OVR surface for material: %zu", materialIndex);
		return ovr_surface_def;
	}

	gl_vb->CreateVAO(gl_ib->GetIndexID());

	std::shared_ptr<scc::GL_Skin> skin = std::dynamic_pointer_cast<scc::GL_Skin>(GetSkin());

	//Update material with Android-specific state.
	scr::Material::MaterialCreateInfo &materialCI = material->GetMaterialCreateInfo();
	materialCI.effect = &globalGraphicsResources.defaultPBREffect;

	if(materialCI.diffuse.texture)
	{
		materialCI.diffuse.texture->UseSampler(globalGraphicsResources.sampler);
	}
	if(materialCI.normal.texture)
	{
		materialCI.normal.texture->UseSampler(globalGraphicsResources.sampler);
	}
	if(materialCI.combined.texture)
	{
		materialCI.combined.texture->UseSampler(globalGraphicsResources.sampler);
	}

	//Get effect pass create info.
	std::string completePassName = GetCompleteEffectPassName(globalGraphicsResources.effectPassName);
	const scc::GL_Effect& gl_effect = globalGraphicsResources.defaultPBREffect;
	const scr::Effect::EffectPassCreateInfo* effectPassCreateInfo = gl_effect.GetEffectPassCreateInfo(completePassName.c_str());

	//Construct OVR::GLGeometry from mesh data.
	OVRFW::GlGeometry geo;
	geo.vertexBuffer = gl_vb->GetVertexID();
	geo.indexBuffer = gl_ib->GetIndexID();
	geo.vertexArrayObject = gl_vb->GetVertexArrayID();
	geo.primitiveType = scc::GL_Effect::ToGLTopology(effectPassCreateInfo->topology);
	geo.vertexCount = (int)gl_vb->GetVertexCount();
	geo.indexCount = (int)gl_ib->GetIndexBufferCreateInfo().indexCount;
	OVRFW::GlGeometry::IndexType = gl_ib->GetIndexBufferCreateInfo().stride == 4 ? GL_UNSIGNED_INT : gl_ib->GetIndexBufferCreateInfo().stride == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE;

	//Initialise surface definition.
	std::string _nodeName = std::string("NodeID: ") + std::to_string(id);
	ovr_surface_def.surfaceName = _nodeName;
	ovr_surface_def.numInstances = 1;
	ovr_surface_def.geo = geo;

	//Set Rendering Set
	if(effectPassCreateInfo)
	{
		ovr_surface_def.graphicsCommand.GpuState.blendMode				= scc::GL_Effect::ToGLBlendOp(effectPassCreateInfo->colourBlendingState.colorBlendOp);
		ovr_surface_def.graphicsCommand.GpuState.blendSrc				= scc::GL_Effect::ToGLBlendFactor(effectPassCreateInfo->colourBlendingState.srcColorBlendFactor);
		ovr_surface_def.graphicsCommand.GpuState.blendDst				= scc::GL_Effect::ToGLBlendFactor(effectPassCreateInfo->colourBlendingState.dstColorBlendFactor);
		ovr_surface_def.graphicsCommand.GpuState.blendModeAlpha			= scc::GL_Effect::ToGLBlendOp(effectPassCreateInfo->colourBlendingState.alphaBlendOp);
		ovr_surface_def.graphicsCommand.GpuState.blendSrcAlpha			= scc::GL_Effect::ToGLBlendFactor(effectPassCreateInfo->colourBlendingState.srcAlphaBlendFactor);
		ovr_surface_def.graphicsCommand.GpuState.blendDstAlpha			= scc::GL_Effect::ToGLBlendFactor(effectPassCreateInfo->colourBlendingState.dstAlphaBlendFactor);
		ovr_surface_def.graphicsCommand.GpuState.depthFunc				= scc::GL_Effect::ToGLCompareOp(effectPassCreateInfo->depthStencilingState.depthCompareOp);

		ovr_surface_def.graphicsCommand.GpuState.frontFace				= skin ? GL_CW : GL_CCW;
		ovr_surface_def.graphicsCommand.GpuState.polygonMode			= scc::GL_Effect::ToGLPolygonMode(effectPassCreateInfo->rasterizationState.polygonMode);
		ovr_surface_def.graphicsCommand.GpuState.blendEnable			= effectPassCreateInfo->colourBlendingState.blendEnable ? OVRFW::ovrGpuState::ovrBlendEnable::BLEND_ENABLE : OVRFW::ovrGpuState::ovrBlendEnable::BLEND_DISABLE;
		ovr_surface_def.graphicsCommand.GpuState.depthEnable			= effectPassCreateInfo->depthStencilingState.depthTestEnable;
		ovr_surface_def.graphicsCommand.GpuState.depthMaskEnable 		= true;
		ovr_surface_def.graphicsCommand.GpuState.colorMaskEnable[0]		= true;
		ovr_surface_def.graphicsCommand.GpuState.colorMaskEnable[1]		= true;
		ovr_surface_def.graphicsCommand.GpuState.colorMaskEnable[2]		= true;
		ovr_surface_def.graphicsCommand.GpuState.colorMaskEnable[3]		= true;
		ovr_surface_def.graphicsCommand.GpuState.polygonOffsetEnable	= false;
		ovr_surface_def.graphicsCommand.GpuState.cullEnable				= effectPassCreateInfo->rasterizationState.cullMode != scr::Effect::CullMode::NONE;
		ovr_surface_def.graphicsCommand.GpuState.lineWidth				= 1.0F;
		ovr_surface_def.graphicsCommand.GpuState.depthRange[0]			= effectPassCreateInfo->depthStencilingState.minDepthBounds;
		ovr_surface_def.graphicsCommand.GpuState.depthRange[1]			= effectPassCreateInfo->depthStencilingState.maxDepthBounds;
	}

	//Fill shader resources vector.
	std::vector<const scr::ShaderResource*> pbrShaderResources;
	pbrShaderResources.push_back(&globalGraphicsResources.scrCamera->GetShaderResource());
	pbrShaderResources.push_back(&(skin ? skin->GetShaderResource() : globalGraphicsResources.defaultSkin.GetShaderResource()));
	pbrShaderResources.push_back(&material->GetShaderResource());
	pbrShaderResources.push_back(&globalGraphicsResources.lightCubemapShaderResources);

	//Set image samplers and uniform buffers.
	size_t resourceCount = 0;
	GLint textureCount = 0, uniformCount = 0;
	size_t j = 0;
	for(const scr::ShaderResource *sr : pbrShaderResources)
	{
		const std::vector<scr::ShaderResource::WriteShaderResource> &shaderResourceSet = sr->GetWriteShaderResources();
		for(const scr::ShaderResource::WriteShaderResource& resource : shaderResourceSet)
		{
			scr::ShaderResourceLayout::ShaderResourceType type = resource.shaderResourceType;
			if(type == scr::ShaderResourceLayout::ShaderResourceType::COMBINED_IMAGE_SAMPLER)
			{
				if(resource.imageInfo.texture.get())
				{
					auto gl_texture = dynamic_cast<scc::GL_Texture *>(resource.imageInfo.texture.get());
					ovr_surface_def.graphicsCommand.UniformData[j].Data = &(gl_texture->GetGlTexture());
					textureCount++;
				}
			}
			else if(type == scr::ShaderResourceLayout::ShaderResourceType::UNIFORM_BUFFER)
			{
				if(resource.bufferInfo.buffer)
				{
					scc::GL_UniformBuffer *gl_uniformBuffer = static_cast<scc::GL_UniformBuffer *>(resource.bufferInfo.buffer);
					ovr_surface_def.graphicsCommand.UniformData[j].Data = &(gl_uniformBuffer->GetGlBuffer());
					uniformCount++;
				}
			}
			else
			{
				//NULL
			}
			j++;
			resourceCount++;
			assert(resourceCount <= OVRFW::ovrUniform::MAX_UNIFORMS);
			assert(textureCount <= globalGraphicsResources.maxFragTextureSlots);
			assert(uniformCount <= globalGraphicsResources.maxFragUniformBlocks);
		}
	}

	return ovr_surface_def;
}

void OVRNode::RefreshOVRSurfaces()
{
	ovrSurfaceDefs.clear();

	//We can't create surfaces without a mesh, so we should leave the list empty.
	std::shared_ptr<scr::Mesh> mesh = GetMesh();
	if(!mesh)
	{
		return;
	}

	std::vector<std::shared_ptr<scr::Material>> materials = GetMaterials();
	ovrSurfaceDefs.resize(materials.size());
	for(size_t i = 0; i < materials.size(); i++)
	{
		ovrSurfaceDefs[i] = CreateOVRSurface(i, materials[i]);
	}
}