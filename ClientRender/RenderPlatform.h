// (C) Copyright 2018-2022 Simul Software Ltd
#pragma once

#include "Common.h"

#include "ClientRender/API.h"
#include "TeleportClient/basic_linear_algebra.h"

namespace clientrender
{
	class FrameBuffer;
	class IndexBuffer;
	class Effect;
	class Sampler;
	class Shader;
	class ShaderStorageBuffer;
	class Skin;
	class Texture;
	class Transform;
	class UniformBuffer;
	class VertexBuffer;

	class Material;

	class RenderPlatform
	{
	public:
		virtual ~RenderPlatform() {}

		virtual API::APIType GetAPI() const = 0;

		virtual std::shared_ptr<FrameBuffer>			InstantiateFrameBuffer() const = 0;
		virtual std::shared_ptr<IndexBuffer>			InstantiateIndexBuffer() const = 0;
		virtual std::shared_ptr<Effect>					InstantiateEffect() const = 0;
		virtual std::shared_ptr<Sampler>				InstantiateSampler() const = 0;
		virtual std::shared_ptr<Shader>					InstantiateShader() const = 0;
		virtual std::shared_ptr<ShaderStorageBuffer>	InstantiateShaderStorageBuffer() const = 0;
		virtual std::shared_ptr<Skin>					InstantiateSkin(const std::string& name) const = 0;
		virtual std::shared_ptr<Skin>					InstantiateSkin(const std::string& name, const std::vector<clientrender::mat4>& inverseBindMatrices, size_t numBones, const Transform& skinTransform) const = 0;
		virtual std::shared_ptr<Texture>				InstantiateTexture() const = 0;
		virtual std::shared_ptr<UniformBuffer>			InstantiateUniformBuffer() const = 0;
		virtual std::shared_ptr<VertexBuffer>			InstantiateVertexBuffer() const = 0;

		std::shared_ptr<clientrender::Material> placeholderMaterial;
	};
}