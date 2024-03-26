// (C) Copyright 2018-2022 Simul Software Ltd
#pragma once

#include "Common.h"

namespace teleport
{
	namespace clientrender
	{
		class VertexBufferLayout
		{
		public:
			enum class Type : uint32_t
			{
				FLOAT,
				DOUBLE,
				HALF,
				UINT,
				USHORT,
				UBYTE,
				INT,
				SHORT,
				BYTE,
			};
			enum class ComponentCount : uint32_t
			{
				SCALAR = 1,
				VEC2,
				VEC3,
				VEC4
			};
			enum class PackingStyle : uint32_t
			{
				GROUPED,	// e.g. VVVVNNNNCCCC
				INTERLEAVED // e.g. VNCVNCVNCVNC
			};
			struct VertexAttribute
			{
				uint32_t location;
				ComponentCount componentCount;
				Type type;
			};

		public:
			PackingStyle m_PackingStyle;
			std::vector<VertexAttribute> m_Attributes;
			size_t m_Stride = 0; // Value in Bytes

			inline void AddAttribute(const VertexAttribute &attribute)
			{
				m_Attributes.push_back(attribute);
			}

			inline void AddAttribute(uint32_t location, ComponentCount count, Type type)
			{
				m_Attributes.push_back({location, count, type});
			}
			inline void CalculateStride()
			{
				for (auto &attrib : m_Attributes)
				{
					m_Stride += static_cast<size_t>(attrib.componentCount) * GetAttributeTypeSize(attrib.type);
				}
			}
			inline size_t GetAttributeTypeSize(Type type)
			{
				switch (type)
				{
				case Type::DOUBLE:
					return 8;
				case Type::FLOAT:
				case Type::UINT:
				case Type::INT:
					return 4;
				case Type::USHORT:
				case Type::SHORT:
					return 2;
				case Type::UBYTE:
				case Type::BYTE:
				default:
					return 1;
				}
			}
		};
	}
}