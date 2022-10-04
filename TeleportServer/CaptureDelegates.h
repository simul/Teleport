#pragma once

#include <functional>

#include "libavstream/common_maths.h"

namespace teleport
{
	struct CasterContext;

	struct CameraInfo
	{
		avs::vec4 orientation = {0, 0, 0, 1};
		avs::vec3 position = {0, 0, 0};
		float fov = 90;
		float width = 1920;
		float height = 1080;
		bool isVR = false;
	};

	struct CaptureDelegates
	{
		std::function<void(teleport::CasterContext* context)> startStreaming;

		std::function<void(void)> requestKeyframe;

		std::function<teleport::CameraInfo&(void)> getClientCameraInfo;
	};
}