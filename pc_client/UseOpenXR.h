#pragma once
#include "Platform/CrossPlatform/RenderPlatform.h"
#include <vector>
#include "Platform/CrossPlatform/RenderDelegate.h"
#include "Platform/CrossPlatform/Texture.h"
#include "TeleportClient/OpenXR.h"
#include "common_maths.h"			// for avs::Pose
#include "TeleportCore/CommonNetworking.h"		// for avs::InputState
#include "TeleportCore/Input.h"

namespace teleport
{
	class UseOpenXR: public client::OpenXR
	{
	public:
		bool TryInitDevice() override;
		void SetCurrentFrameDeviceContext(platform::crossplatform::MultiviewGraphicsDeviceContext& d)
		{
			deviceContext = &d;
		}
	protected:
		const char *GetOpenXRGraphicsAPIExtensionName() const override;
		platform::crossplatform::MultiviewGraphicsDeviceContext& GetDeviceContext(size_t, size_t) override
		{
			return *deviceContext;
		}
		platform::crossplatform::MultiviewGraphicsDeviceContext* deviceContext = nullptr;
	};
}
