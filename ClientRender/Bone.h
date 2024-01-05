#pragma once

#include <memory>

#include "Transform.h"

namespace teleport
{
	namespace clientrender
	{
		class Bone
		{
		public:
			Bone(avs::uid id, const std::string &name);
			~Bone();
			std::string getName() const
			{
				return name;
			}

			const avs::uid id;
			const std::string name;

			void SetParent(std::shared_ptr<Bone> parent);
			const std::shared_ptr<Bone> GetParent() const;

			const std::vector<std::weak_ptr<Bone>> &GetChildren() const
			{
				return children;
			}

			void AddChild(std::shared_ptr<Bone> child);
			void RemoveChild(std::shared_ptr<Bone> child);
			void RemoveChild(avs::uid childID);

			void SetLocalTransform(const Transform &transform);
			const Transform &GetLocalTransform() const;

			// Force an update on the bone's global transform.
			void UpdateGlobalTransform() const;
			const Transform &GetGlobalTransform() const;

		private:
			std::weak_ptr<Bone> parent;
			std::vector<std::weak_ptr<Bone>> children;

			Transform localTransform;

			mutable Transform globalTransform; // Cached transform, which is the bone's transform in global space.
			mutable bool isGlobalTransformDirty = false;
		};
	}

}