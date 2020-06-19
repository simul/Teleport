#include "Transform.h"


namespace scr
{

//Transform
bool Transform::s_UninitialisedUB = true;
std::shared_ptr<UniformBuffer> Transform::s_UB = nullptr;

Transform::Transform()
	:Transform(TransformCreateInfo{nullptr}, avs::vec3(), quat(), avs::vec3())
{}

Transform::Transform(const TransformCreateInfo& pTransformCreateInfo)
	: Transform(pTransformCreateInfo, avs::vec3(), quat(), avs::vec3())
{}

Transform::Transform(const TransformCreateInfo& pTransformCreateInfo, avs::vec3 translation, quat rotation, avs::vec3 scale)
	: m_Translation(translation), m_Rotation(rotation), m_Scale(scale)
{
	if(false)//s_UninitialisedUB)
	{
		UniformBuffer::UniformBufferCreateInfo ub_ci;
		ub_ci.bindingLocation = 1;
		ub_ci.size = sizeof(TransformData);
		ub_ci.data = &m_TransformData;

		s_UB = m_CI.renderPlatform->InstantiateUniformBuffer();
		s_UB->Create(&ub_ci);
		s_UninitialisedUB = false;
	}

	m_ShaderResourceLayout.AddBinding(1, ShaderResourceLayout::ShaderResourceType::UNIFORM_BUFFER, Shader::Stage::SHADER_STAGE_VERTEX);

	m_ShaderResource = ShaderResource({m_ShaderResourceLayout});
	m_ShaderResource.AddBuffer(0, ShaderResourceLayout::ShaderResourceType::UNIFORM_BUFFER, 1, "u_ActorUBO", {s_UB.get(), 0, sizeof(TransformData)});

	m_TransformData.m_ModelMatrix = mat4::Translation(translation) * mat4::Rotation(rotation) * mat4::Scale(scale);
}

void Transform::UpdateModelMatrix(const avs::vec3& translation, const quat& rotation, const avs::vec3& scale)
{
	m_Translation = translation;
	m_Rotation = rotation;
	m_Scale = scale;
	m_TransformData.m_ModelMatrix = mat4::Translation(translation) * mat4::Rotation(rotation) * mat4::Scale(scale);
}

}