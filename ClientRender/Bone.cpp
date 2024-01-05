#include "Bone.h"
#include "TeleportCore/ErrorHandling.h"

namespace teleport
{
	namespace clientrender
	{
		Bone::Bone(avs::uid id, const std::string &name)
			: id(id), name(name)
		{
		}

		Bone::~Bone()
		{
			std::shared_ptr<clientrender::Bone> parentPtr = parent.lock();
			if (parentPtr)
			{
				parentPtr->RemoveChild(id);
			}

			for (std::weak_ptr<clientrender::Bone> weak_child : children)
			{
				std::shared_ptr<clientrender::Bone> child = weak_child.lock();
				if (child)
				{
					child->SetParent(nullptr);
				}
			}
		}

		void Bone::SetParent(std::shared_ptr<Bone> parent)
		{
			if (parent.get() == this)
			{
				TELEPORT_CERR << "Trying to set bone as its own parent.\n";
				return;
			}
			this->parent = parent;
			isGlobalTransformDirty = true;
		}

		const std::shared_ptr<Bone> Bone::GetParent() const
		{
			return parent.lock();
		}

		void Bone::AddChild(std::shared_ptr<Bone> child)
		{
			children.push_back(child);
		}

		void Bone::RemoveChild(std::shared_ptr<Bone> child)
		{
			RemoveChild(child->id);
		}

		void Bone::RemoveChild(avs::uid childID)
		{
			for (auto it = children.begin(); it != children.end();)
			{
				if (it->expired() || it->lock()->id == childID)
				{
					it = children.erase(it);
				}
				else
				{
					++it;
				}
			}
		}

		void Bone::SetLocalTransform(const Transform &transform)
		{
			localTransform = transform;
			isGlobalTransformDirty = true;
		}

		const Transform &Bone::GetLocalTransform() const
		{
			return localTransform;
		}

		void Bone::UpdateGlobalTransform() const
		{
			std::shared_ptr<Bone> parentPtr = parent.lock();
			globalTransform = parentPtr ? localTransform * parentPtr->GetGlobalTransform() : localTransform;
			isGlobalTransformDirty = false;

			for (std::weak_ptr<clientrender::Bone> weak_child : children)
			{
				weak_child.lock()->UpdateGlobalTransform();
			}
		}

		const Transform &Bone::GetGlobalTransform() const
		{
			if (isGlobalTransformDirty)
			{
				UpdateGlobalTransform();
			}

			return globalTransform;
		}
	}
}