// (C) Copyright 2018-2023 Simul Software Ltd
#pragma once
#include "MemoryUtil.h"
#include "libavstream/common.hpp"
#include <set>
#include <unordered_map>

namespace clientrender
{
	typedef unsigned long long geometry_cache_uid;
	class Node;
	class Skeleton;
	class Material;
	struct IncompleteResource
	{
		IncompleteResource(avs::uid id, avs::GeometryPayloadType type)
			: id(id), type(type)
		{
		}

		const avs::uid id;
		const avs::GeometryPayloadType type;

		void IncrementMissingResources()
		{
			missingResourceCount++;
		}

		void DecrementMissingResources();

		uint32_t GetMissingResourceCount() const
		{
			return missingResourceCount;
		}

	private:
		uint32_t missingResourceCount = 0;
	};

	struct MissingResource
	{
		const avs::uid id;					   // ID of the missing resource.
		avs::GeometryPayloadType resourceType; // missing resource's type.
		// Resources that can't be completed without this missing resource.
		std::set<std::shared_ptr<IncompleteResource>> waitingResources;

		MissingResource(avs::uid id, avs::GeometryPayloadType r)
			: id(id), resourceType(r)
		{
		}
	};
	struct IncompleteFontAtlas : IncompleteResource
	{
		IncompleteFontAtlas(avs::uid id)
			: IncompleteResource(id, avs::GeometryPayloadType::FontAtlas)
		{
		}
		avs::uid missingTextureUid = 0;
	};
	struct IncompleteTextCanvas : IncompleteResource
	{
		IncompleteTextCanvas(avs::uid id)
			: IncompleteResource(id, avs::GeometryPayloadType::TextCanvas)
		{
		}
		avs::uid missingFontAtlasUid = 0;
	};

	struct IncompleteNode : IncompleteResource
	{
		IncompleteNode(avs::uid id)
			: IncompleteResource(id, avs::GeometryPayloadType::Node)
		{
		}

		std::unordered_map<avs::uid, std::vector<size_t>> materialSlots; //<ID of the material, list of indexes the material should be placed into node material list>.
																		 // std::unordered_map<avs::uid, size_t> missingAnimations;				//<ID of missing animation, index in animation vector>
		// std::set<avs::uid> missingNodes;									//<e.g. missing skeleton nodes.
	};

	struct IncompleteSkeleton : IncompleteResource
	{
		IncompleteSkeleton(avs::uid id, avs::GeometryPayloadType type)
			: IncompleteResource(id, type)
		{
		}

		std::shared_ptr<clientrender::Skeleton> skeleton;

		std::unordered_map<avs::uid, size_t> missingBones; //<ID of missing bone, index in vector>
	};

}