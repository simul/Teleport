#pragma once

struct FRemotePlayContext
{
	TUniquePtr<IEncodePipeline> EncodePipeline;
	TUniquePtr<FNetworkPipeline> NetworkPipeline;
	TUniquePtr<avs::Queue> ColorQueue;
	TUniquePtr<avs::Queue> DepthQueue;
	TUniquePtr<avs::Queue> GeometryQueue;
	bool bCaptureDepth = false;
	avs::AxesStandard axesStandard=avs::AxesStandard::NotInitialized;
};
