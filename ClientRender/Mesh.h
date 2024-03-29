// (C) Copyright 2018-2022 Simul Software Ltd
#pragma once

#include "ClientRender/IndexBuffer.h"
#include "ClientRender/Material.h"
#include "ClientRender/VertexBuffer.h"

namespace teleport
{
	namespace clientrender
	{
		class Mesh
		{
		public:
			struct MeshCreateInfo
			{
				std::string name;
				avs::uid id;
				std::vector<std::shared_ptr<VertexBuffer>> vertexBuffers;
				std::vector<std::shared_ptr<IndexBuffer>> indexBuffers;
				std::vector<std::shared_ptr<Material::MaterialCreateInfo>> internalMaterials;
				std::vector<mat4> inverseBindMatrices;

				bool clockwiseFaces = true;
			};

		protected:
			MeshCreateInfo m_CI;
			std::vector<std::shared_ptr<Material>> internalMaterials;

		public:
			Mesh(const MeshCreateInfo &pMeshCreateInfo);
			~Mesh();
			std::string getName() const
			{
				return m_CI.name;
			}
			static const char *getTypeName()
			{
				return "Mesh";
			}
			inline const MeshCreateInfo &GetMeshCreateInfo() const { return m_CI; }
			const std::vector<std::shared_ptr<Material>> GetInternalMaterials() const
			{
				return internalMaterials;
			}
		};
	}
}