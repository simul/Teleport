#pragma once

#include <vector>

#include "Animation.h"

namespace scr
{
class AnimationComponent
{
public:
	AnimationComponent();
	AnimationComponent(const std::map<avs::uid, std::shared_ptr<Animation>>& animations);

	void AddAnimation(avs::uid id, std::shared_ptr<Animation> animation);

	void update(float deltaTime);
private:
	std::map<avs::uid, std::shared_ptr<Animation>> animations;
	std::map<avs::uid, std::shared_ptr<Animation>>::iterator currentAnimation = animations.begin();
};

class VisibilityComponent
{
public:
	void update(float deltaTime);

	void setVisibility(bool visible);
	bool getVisibility() const;

	float getTimeSinceLastVisible() const;

private:
	bool isVisible = true;

	float timeSinceLastVisible = 0;
};

}