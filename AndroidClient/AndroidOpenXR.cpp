#if 1
#define XR_USE_GRAPHICS_API_VULKAN 1
#define XR_USE_PLATFORM_ANDROID 1
//#define XR_EXTENSION_PROTOTYPES 1
#include "AndroidOpenXR.h"
#include <android/log.h>
#include <sys/system_properties.h>
#include <vulkan/vulkan.hpp>
#include <openxr/openxr.h>
#define XR_USE_TIMESPEC 1
#include <openxr/openxr_platform.h>
#include <openxr/openxr_oculus.h>
#include <openxr/openxr_oculus_helpers.h>
#include <fmt/core.h>
#include <sstream>
#include <iostream>
#include "TeleportCore/ErrorHandling.h"
#include "TeleportClient/Log.h"
#include "Platform/Vulkan/RenderPlatform.h"

std::string teleport::android::vkResultString(VkResult res)
{
	switch (res)
	{
		case VK_SUCCESS:
			return "SUCCESS";
		case VK_NOT_READY:
			return "NOT_READY";
		case VK_TIMEOUT:
			return "TIMEOUT";
		case VK_EVENT_SET:
			return "EVENT_SET";
		case VK_EVENT_RESET:
			return "EVENT_RESET";
		case VK_INCOMPLETE:
			return "INCOMPLETE";
		case VK_ERROR_OUT_OF_HOST_MEMORY:
			return "ERROR_OUT_OF_HOST_MEMORY";
		case VK_ERROR_OUT_OF_DEVICE_MEMORY:
			return "ERROR_OUT_OF_DEVICE_MEMORY";
		case VK_ERROR_INITIALIZATION_FAILED:
			return "ERROR_INITIALIZATION_FAILED";
		case VK_ERROR_DEVICE_LOST:
			return "ERROR_DEVICE_LOST";
		case VK_ERROR_MEMORY_MAP_FAILED:
			return "ERROR_MEMORY_MAP_FAILED";
		case VK_ERROR_LAYER_NOT_PRESENT:
			return "ERROR_LAYER_NOT_PRESENT";
		case VK_ERROR_EXTENSION_NOT_PRESENT:
			return "ERROR_EXTENSION_NOT_PRESENT";
		case VK_ERROR_FEATURE_NOT_PRESENT:
			return "ERROR_FEATURE_NOT_PRESENT";
		case VK_ERROR_INCOMPATIBLE_DRIVER:
			return "ERROR_INCOMPATIBLE_DRIVER";
		case VK_ERROR_TOO_MANY_OBJECTS:
			return "ERROR_TOO_MANY_OBJECTS";
		case VK_ERROR_FORMAT_NOT_SUPPORTED:
			return "ERROR_FORMAT_NOT_SUPPORTED";
		case VK_ERROR_SURFACE_LOST_KHR:
			return "ERROR_SURFACE_LOST_KHR";
		case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
			return "ERROR_NATIVE_WINDOW_IN_USE_KHR";
		case VK_SUBOPTIMAL_KHR:
			return "SUBOPTIMAL_KHR";
		case VK_ERROR_OUT_OF_DATE_KHR:
			return "ERROR_OUT_OF_DATE_KHR";
		case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR:
			return "ERROR_INCOMPATIBLE_DISPLAY_KHR";
		case VK_ERROR_VALIDATION_FAILED_EXT:
			return "ERROR_VALIDATION_FAILED_EXT";
		case VK_ERROR_INVALID_SHADER_NV:
			return "ERROR_INVALID_SHADER_NV";
		default:
			return std::to_string(res);
	}
}

using namespace platform;
using namespace teleport;
using namespace android;
using namespace std;

const XrPosef	xr_pose_identity = { {0,0,0,1}, {0,0,0} };
#define TELEPORT_LOG_TAG "Teleport"
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, TELEPORT_LOG_TAG, __VA_ARGS__)
#define ALOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, TELEPORT_LOG_TAG, __VA_ARGS__)

#if defined(DEBUG)
void OXR_CheckErrors(XrInstance instance, XrResult result, const char* function, bool failOnError)
{
	if (XR_FAILED(result))
	{
		char errorBuffer[XR_MAX_RESULT_STRING_SIZE];
		xrResultToString(instance, result, errorBuffer);
		if (failOnError)
		{
			ALOGE("OpenXR error: %s: %s\n", function, errorBuffer);
		}
		else
		{
			ALOGV("OpenXR error: %s: %s\n", function, errorBuffer);
		}
	}
}
#endif

#include "Platform/Vulkan/RenderPlatform.h"
client::swapchain_surfdata_t CreateSurfaceData(crossplatform::RenderPlatform* renderPlatform, XrBaseInStructure& swapchain_img, XrSwapchainCreateInfo swapchain_info)
{
	client::swapchain_surfdata_t result = {};

	// Get information about the swapchain image that OpenXR made for us!
	XrSwapchainImageVulkanKHR& vulkan_swapchain_img = (XrSwapchainImageVulkanKHR&)swapchain_img;
	platform::crossplatform::PixelFormat pixelFormat = platform::vulkan::RenderPlatform::FromVulkanFormat((vk::Format)swapchain_info.format);

	platform::crossplatform::TextureCreate textureCreate = {};
	textureCreate.external_texture = vulkan_swapchain_img.image;
	textureCreate.w = swapchain_info.width;
	textureCreate.l = swapchain_info.height;
	textureCreate.arraysize = swapchain_info.arraySize;
	textureCreate.make_rt = true;
	textureCreate.f = pixelFormat;
	textureCreate.numOfSamples = std::max((uint32_t)1, swapchain_info.sampleCount);
	result.target_view = renderPlatform->CreateTexture("swapchain target");
	result.target_view->InitFromExternalTexture(renderPlatform, &textureCreate);
	
	textureCreate.external_texture = nullptr;
	textureCreate.w = result.target_view->width;
	textureCreate.l = result.target_view->length;
	textureCreate.f = platform::crossplatform::PixelFormat::D_32_FLOAT;
	textureCreate.make_rt = false;
	textureCreate.setDepthStencil = true;
	textureCreate.numOfSamples = std::max(1, result.target_view->GetSampleCount());
	result.depth_view = renderPlatform->CreateTexture("swapchain depth");
	result.depth_view->EnsureTexture(renderPlatform, &textureCreate);
	return result;
}

OpenXR::OpenXR(JavaVM *vm,jobject clazz):client::OpenXR("Teleport VR")
{
    PFN_xrInitializeLoaderKHR xrInitializeLoaderKHR;
    xrGetInstanceProcAddr(
        XR_NULL_HANDLE, "xrInitializeLoaderKHR", (PFN_xrVoidFunction*)&xrInitializeLoaderKHR);
    if (!xrInitializeLoaderKHR ) 
		return;
	XrLoaderInitInfoAndroidKHR loaderInitializeInfoAndroid;
	memset(&loaderInitializeInfoAndroid, 0, sizeof(loaderInitializeInfoAndroid));
	loaderInitializeInfoAndroid.type = XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR;
	loaderInitializeInfoAndroid.next = NULL;
	loaderInitializeInfoAndroid.applicationVM = vm;
	loaderInitializeInfoAndroid.applicationContext = clazz;
	xrInitializeLoaderKHR((XrLoaderInitInfoBaseHeaderKHR*)&loaderInitializeInfoAndroid);
}

void OpenXR::SetVulkanDeviceAndInstance(vk::Device *vkDevice,vk::Instance *vkInstance,int m_tid,int r_tid)
{
	MainThreadTid=m_tid;
	RenderThreadTid=r_tid;
	vulkanDevice=vkDevice;
	vulkanInstance=vkInstance;
	vulkanQueue=vulkanDevice->getQueue(0,0);
}

std::vector<std::string> OpenXR::GetRequiredVulkanDeviceExtensions() const
{
	std::vector<std::string> lst;
	uint32_t capacity=0;
	PFN_xrGetVulkanDeviceExtensionsKHR xrGetVulkanDeviceExtensionsKHR = nullptr;
	xrGetInstanceProcAddr(xr_instance, "xrGetVulkanDeviceExtensionsKHR", (PFN_xrVoidFunction*)(&xrGetVulkanDeviceExtensionsKHR));
	if(!xrGetVulkanDeviceExtensionsKHR)
		return lst;
	if (!CheckXrResult(xr_instance,xrGetVulkanDeviceExtensionsKHR(xr_instance,xr_system_id,0,&capacity,nullptr)))
		return lst;
	std::string all_extensions;
	all_extensions.resize(capacity);
	if (!CheckXrResult(xr_instance,xrGetVulkanDeviceExtensionsKHR(xr_instance,xr_system_id,all_extensions.size(),&capacity,all_extensions.data())))
		return lst;
	
    //Construct a stream from the string
    std::stringstream streamData(all_extensions);
    /*
    Declare string variable that will be used
    to store data after split
    */
    std::string val;
    /*
    The loop will iterate the splitted data and
    insert the data into the array
    */
	while (std::getline(streamData, val, ' '))
	{
		lst.push_back(val);
	}
	return lst;
}

std::vector<std::string> OpenXR::GetRequiredVulkanInstanceExtensions() const
{
	if(!xr_instance)
	{
	TELEPORT_INTERNAL_CERR("No instance for GetRequiredVulkanInstanceExtensions\n");
	}
	std::vector<std::string> lst;
	uint32_t capacity=0;
	PFN_xrGetVulkanInstanceExtensionsKHR xrGetVulkanInstanceExtensionsKHR = nullptr;
	xrGetInstanceProcAddr(xr_instance, "xrGetVulkanInstanceExtensionsKHR", (PFN_xrVoidFunction*)(&xrGetVulkanInstanceExtensionsKHR));
	if(!xrGetVulkanInstanceExtensionsKHR)
		return lst;
	if (!CheckXrResult(xr_instance,xrGetVulkanInstanceExtensionsKHR(xr_instance,xr_system_id,0,&capacity,nullptr)))
		return lst;
	std::string all_extensions;
	all_extensions.resize(capacity);
	if (!CheckXrResult(xr_instance,xrGetVulkanInstanceExtensionsKHR(xr_instance,xr_system_id,all_extensions.size(),&capacity,all_extensions.data())))
		return lst;
	
    //Construct a stream from the string
    std::stringstream streamData(all_extensions);
    /*
    Declare string variable that will be used
    to store data after split
    */
    std::string val;
    /*
    The loop will iterate the splitted data and
    insert the data into the array
    */
	while (std::getline(streamData, val, ' '))
	{
		lst.push_back(val);
	}
	return lst;
}

bool OpenXR::InitSystem()
{
	// Request a form factor from the device (HMD, Handheld, etc.)
	// If the device is not on, not connected, or its app is not running, this may fail here:
	XrSystemGetInfo systemInfo = { XR_TYPE_SYSTEM_GET_INFO };
	systemInfo.formFactor = app_config_form;
	if (!CheckXrResult(xr_instance, xrGetSystem(xr_instance, &systemInfo, &xr_system_id)))
	{
		std::cerr << fmt::format("Failed to Get XR System\n").c_str() << std::endl;
		return false;
	}
	    // AppSpaceWarp Get recommended motion vector resolution, we don't recommend to use higher
    // motion vector resolution than recommended It won't give you extra quality improvement even it
    // make your motion vector pass more expensive.
    spaceWarpProperties.type = XR_TYPE_SYSTEM_SPACE_WARP_PROPERTIES_FB;

	xr_system_properties.type = XR_TYPE_SYSTEM_PROPERTIES;
	// Check if hand tracking is supported.
	xr_system_properties.next = &handTrackingSystemProperties;
	handTrackingSystemProperties.next = &spaceWarpProperties;

    XR_CHECK(xrGetSystemProperties(xr_instance, xr_system_id, &xr_system_properties));

    TELEPORT_COUT<<fmt::format(
        "System Properties: Name={} VendorId={}",
		xr_system_properties.systemName,
		xr_system_properties.vendorId)<<"\n";
	TELEPORT_COUT << fmt::format(
		"handTracking Properties: Supported {}",
						 handTrackingSystemProperties.supportsHandTracking != 0)
				  << "\n";
	TELEPORT_COUT << fmt::format(
        "System Graphics Properties: MaxWidth={} MaxHeight={} MaxLayers={}",
		xr_system_properties.graphicsProperties.maxSwapchainImageWidth,
		xr_system_properties.graphicsProperties.maxSwapchainImageHeight,
						 xr_system_properties.graphicsProperties.maxLayerCount)
				  << "\n";
	TELEPORT_COUT << fmt::format(
        "System Tracking Properties: OrientationTracking={} PositionTracking={}",
		xr_system_properties.trackingProperties.orientationTracking ? "True" : "False",
						 xr_system_properties.trackingProperties.positionTracking ? "True" : "False")
				  << "\n";

    TELEPORT_COUT << fmt::format(
        "SpaceWarp Properties: recommendedMotionVectorImageRectWidth={} recommendedMotionVectorImageRectHeight={}",
        spaceWarpProperties.recommendedMotionVectorImageRectWidth,
						 spaceWarpProperties.recommendedMotionVectorImageRectHeight)
				  << "\n";
	return true;
}

void OpenXR::HandleSessionStateChanges( XrSessionState state)
{
    if (state == XR_SESSION_STATE_READY)
	{
        XrSessionBeginInfo sessionBeginInfo;
        memset(&sessionBeginInfo, 0, sizeof(sessionBeginInfo));
        sessionBeginInfo.type = XR_TYPE_SESSION_BEGIN_INFO;
        sessionBeginInfo.next = NULL;
        sessionBeginInfo.primaryViewConfigurationType = app_config_view;

        XrResult result;
        result = xrBeginSession(xr_session, &sessionBeginInfo);
		if(result!=XR_SUCCESS)
		{
			std::cerr<<"Begin OpenXR Session failed."<<std::endl;
		}
		xr_session_running=(result==XR_SUCCESS);
		if(sessionChangedCallback)
			sessionChangedCallback(xr_session_running);
		if (xr_session_running)
		{
			AttachSessionActions();
			if (handTrackingSystemProperties.supportsHandTracking)
				CreateHandTrackers();
		}
        // Set session state once we have entered VR mode and have a valid session object.
        if (result == XR_SUCCESS)
		{
            XrPerfSettingsLevelEXT cpuPerfLevel = XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT;
			int CpuLevel=2;
            switch (CpuLevel)
			{
                case 0:
                    cpuPerfLevel = XR_PERF_SETTINGS_LEVEL_POWER_SAVINGS_EXT;
                    break;
                case 1:
                    cpuPerfLevel = XR_PERF_SETTINGS_LEVEL_SUSTAINED_LOW_EXT;
                    break;
                case 2:
                    cpuPerfLevel = XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT;
                    break;
                case 3:
                    cpuPerfLevel = XR_PERF_SETTINGS_LEVEL_BOOST_EXT;
                    break;
                default:
                    std::cerr<<"Invalid CPU level "<< CpuLevel;
                    break;
            }
			
			int GpuLevel=3;
            XrPerfSettingsLevelEXT gpuPerfLevel = XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT;
            switch (GpuLevel) {
                case 0:
                    gpuPerfLevel = XR_PERF_SETTINGS_LEVEL_POWER_SAVINGS_EXT;
                    break;
                case 1:
                    gpuPerfLevel = XR_PERF_SETTINGS_LEVEL_SUSTAINED_LOW_EXT;
                    break;
                case 2:
                    gpuPerfLevel = XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT;
                    break;
                case 3:
                    gpuPerfLevel = XR_PERF_SETTINGS_LEVEL_BOOST_EXT;
                    break;
                default:
                    std::cerr<<"Invalid GPU level "<< GpuLevel;
                    break;
            }

            PFN_xrPerfSettingsSetPerformanceLevelEXT pfnPerfSettingsSetPerformanceLevelEXT = NULL;
            XR_CHECK(xrGetInstanceProcAddr(
               xr_instance,
                "xrPerfSettingsSetPerformanceLevelEXT",
                (PFN_xrVoidFunction*)(&pfnPerfSettingsSetPerformanceLevelEXT)));

            XR_CHECK(pfnPerfSettingsSetPerformanceLevelEXT(
                xr_session, XR_PERF_SETTINGS_DOMAIN_CPU_EXT, cpuPerfLevel));
            XR_CHECK(pfnPerfSettingsSetPerformanceLevelEXT(
                xr_session, XR_PERF_SETTINGS_DOMAIN_GPU_EXT, gpuPerfLevel));

            PFN_xrSetAndroidApplicationThreadKHR pfnSetAndroidApplicationThreadKHR = NULL;
            XR_CHECK(xrGetInstanceProcAddr(
                xr_instance,
                "xrSetAndroidApplicationThreadKHR",
                (PFN_xrVoidFunction*)(&pfnSetAndroidApplicationThreadKHR)));

            XR_CHECK(pfnSetAndroidApplicationThreadKHR(
                xr_session, XR_ANDROID_THREAD_TYPE_APPLICATION_MAIN_KHR, MainThreadTid));
            XR_CHECK(pfnSetAndroidApplicationThreadKHR(
                xr_session, XR_ANDROID_THREAD_TYPE_RENDERER_MAIN_KHR, RenderThreadTid));
        }
    }
	else if (state == XR_SESSION_STATE_STOPPING)
	{
        XR_CHECK(xrEndSession(xr_session));
		if(sessionChangedCallback)
			sessionChangedCallback(false);
		xr_session_running = false;
    }
	xr_session_state=state;
}

size_t OpenXR::GetCommandBufferIndex(size_t swapchainIndex, size_t imageIndex)
{
	size_t index = 0;
	if (swapchainIndex < xr_swapchains.size())
	{
		for (size_t i = 0; i < swapchainIndex; i++)
			index += xr_swapchains[i].surface_data.size();

		if (imageIndex < xr_swapchains[swapchainIndex].surface_data.size())
			index += imageIndex;
	}
	return index;
}

static platform::crossplatform::MultiviewGraphicsDeviceContext mgdc;
static platform::crossplatform::GraphicsDeviceContext gdc;

platform::crossplatform::GraphicsDeviceContext& OpenXR::GetDeviceContext(size_t swapchainIndex, size_t imageIndex)
{
	platform::crossplatform::GraphicsDeviceContext& deviceContext = swapchainIndex == 0 ? mgdc : gdc;

	// the platform context is the pointer to the VkCommandBuffer.
	size_t i = GetCommandBufferIndex(swapchainIndex, imageIndex);
	CmdBuffer& commandBuffer = cmdBuffers[i];
	deviceContext.platform_context = (void*)&commandBuffer.buf;
	commandBuffer.Reset();
	commandBuffer.Begin();
	return deviceContext;
}

void OpenXR::FinishDeviceContext(size_t swapchainIndex, size_t imageIndex)
{
	platform::crossplatform::GraphicsDeviceContext& deviceContext = swapchainIndex == 0 ? mgdc : gdc;

	//TODO: Find an in-API way of doing this!
	if (deviceContext.renderPlatform && deviceContext.renderPlatform->GetType() == platform::crossplatform::RenderPlatformType::Vulkan)
	{
		vulkan::RenderPlatform* vkrp = (vulkan::RenderPlatform*)deviceContext.renderPlatform;
		vkrp->EndRenderPass(deviceContext);
	}

	size_t i = GetCommandBufferIndex(swapchainIndex, imageIndex);
	CmdBuffer &commandBuffer=cmdBuffers[i];
	commandBuffer.End();
	commandBuffer.Exec(vulkanQueue.operator VkQueue());
	commandBuffer.Wait();
}

void OpenXR::EndFrame() 
{
}

bool OpenXR::StartSession()
{
	RedirectStdCoutCerr();
	// Check what blend mode is valid for this device (opaque vs transparent displays)
	// We'll just take the first one available!
	uint32_t blend_count = 0;
	XR_CHECK(xrEnumerateEnvironmentBlendModes(xr_instance, xr_system_id, app_config_view, 0, &blend_count, nullptr));
	std::vector<XrEnvironmentBlendMode> blend_modes;
	blend_modes.resize(blend_count);
	XR_CHECK(xrEnumerateEnvironmentBlendModes(xr_instance, xr_system_id, app_config_view, blend_count, &blend_count, blend_modes.data()));
	if(std::find(blend_modes.begin(), blend_modes.end(),xr_environment_blend )==blend_modes.end())
	{
		std::cerr<<"Failed to find blend mode.\n";
		return false;
	}
	// OpenXR wants to ensure apps are using the correct graphics card, so this MUST be called 
	// before xrCreateSession. This is crucial on devices that have multiple graphics cards, 
	// like laptops with integrated graphics chips in addition to dedicated graphics cards.

	PFN_xrGetVulkanGraphicsRequirementsKHR ext_xrGetVulkanGraphicsRequirementsKHR = nullptr;
	xrGetInstanceProcAddr(xr_instance, "xrGetVulkanGraphicsRequirementsKHR", (PFN_xrVoidFunction*)(&ext_xrGetVulkanGraphicsRequirementsKHR));
	XrGraphicsRequirementsVulkanKHR requirement = { XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR };
	ext_xrGetVulkanGraphicsRequirementsKHR(xr_instance, xr_system_id, &requirement);
	// A session represents this application's desire to display things! This is where we hook up our graphics API.
	// This does not start the session, for that, you'll need a call to xrBeginSession, which we do in openxr_poll_events
	XrGraphicsBindingVulkanKHR binding = { XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR };
	VkPhysicalDevice vkPhysicalDevice;
	
    PFN_xrGetVulkanGraphicsDeviceKHR xrGetVulkanGraphicsDeviceKHR;
    xrGetInstanceProcAddr(xr_instance, "xrGetVulkanGraphicsDeviceKHR", (PFN_xrVoidFunction*)&xrGetVulkanGraphicsDeviceKHR);
    if (xrGetVulkanGraphicsDeviceKHR != NULL)
	{
		xrGetVulkanGraphicsDeviceKHR(xr_instance,xr_system_id,vulkanInstance->operator VkInstance(),&vkPhysicalDevice);
	}
	binding.device = vulkanDevice->operator VkDevice();
	binding.instance=vulkanInstance->operator VkInstance();
	binding.physicalDevice=vkPhysicalDevice;//vulkanPhysicalDevice->operator VkPhysicalDevice();
	XrSessionCreateInfo sessionInfo = { XR_TYPE_SESSION_CREATE_INFO };
	sessionInfo.next = &binding;
	sessionInfo.systemId = xr_system_id;
	if (!CheckXrResult(xr_instance, xrCreateSession(xr_instance, &sessionInfo, &xr_session)))
	{
		std::cerr << fmt::format("Failed to create XR Session\n").c_str() << std::endl;
		return false;
	}

	// Unable to start a session, may not have an MR device attached or ready
	if (xr_session == nullptr)
		return false;

	// OpenXR uses a couple different types of reference frames for positioning content, we need to choose one for
	// displaying our content! STAGE would be relative to the center of your guardian system's bounds, and LOCAL
	// would be relative to your device's starting location. HoloLens doesn't have a STAGE, so we'll use LOCAL.
	XrReferenceSpaceCreateInfo ref_space = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
	ref_space.poseInReferenceSpace = xr_pose_identity;
	ref_space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
	xrCreateReferenceSpace(xr_session, &ref_space, &xr_app_space);

	ref_space.poseInReferenceSpace = xr_pose_identity;
	ref_space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
	xrCreateReferenceSpace(xr_session, &ref_space, &xr_head_space);

	// Now we need to find all the viewpoints we need to take care of! For a stereo headset, this should be 2.
	// Similarly, for an AR phone, we'll need 1, and a VR cave could have 6, or even 12!
	uint32_t view_count = 0;
	xrEnumerateViewConfigurationViews(xr_instance, xr_system_id, app_config_view, 0, &view_count, nullptr);
	xr_config_views.resize(view_count, { XR_TYPE_VIEW_CONFIGURATION_VIEW });
	xr_views.resize(view_count, { XR_TYPE_VIEW });
	xrEnumerateViewConfigurationViews(xr_instance, xr_system_id, app_config_view, view_count, &view_count, xr_config_views.data());
	
	// Can we override this stuff?
	static int samples = 1;
	for (uint32_t i = 0; i < view_count; i++)
	{
		xr_config_views[i].maxSwapchainSampleCount = samples;
		xr_config_views[i].recommendedSwapchainSampleCount = samples;
	}
	
	// Check the two eyes for stereo are the same
	XrViewConfigurationView& config_view = xr_config_views[0];
	if (app_config_view == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO && xr_config_views.size() == 2)
	{
		SIMUL_ASSERT(xr_config_views[0].recommendedImageRectWidth == xr_config_views[1].recommendedImageRectWidth)
		SIMUL_ASSERT(xr_config_views[0].recommendedImageRectHeight == xr_config_views[1].recommendedImageRectHeight)
	}
	
	// Find out what format to use:
	int64_t swapchain_format = VK_FORMAT_R8G8B8A8_UNORM;
	constexpr int64_t SupportedColorSwapchainFormats[] = {VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM};
	uint32_t formatCount = 0;
	XrResult res = xrEnumerateSwapchainFormats(xr_session, 0, &formatCount, nullptr);
	if (!formatCount)
		return false;

	std::vector<int64_t> availableFormats(formatCount);
	res = xrEnumerateSwapchainFormats(xr_session, formatCount, &formatCount, availableFormats.data());
#if 0
	std::cout << "xrEnumerateSwapchainFormats:\n";
	for (auto f : availableFormats)
	{
		vk::Format F = (vk::Format)f;
		std::cout << "Format: " << vk::to_string(F) << std::endl;
	}
#endif
	auto swapchainFormatIt =std::find_first_of(availableFormats.begin(), availableFormats.end(), std::begin(SupportedColorSwapchainFormats), std::end(SupportedColorSwapchainFormats));
	if (swapchainFormatIt == availableFormats.end())
	{
		throw("No runtime swapchain format supported for color swapchain");
	}
	swapchain_format = *swapchainFormatIt;
	
	//Add XR Swapchain Lambda
	auto AddXrSwapchain = [&, this](XrSwapchainCreateInfo swapchain_info) -> void
	{
		XrSwapchain handle;
		XR_CHECK(xrCreateSwapchain(xr_session, &swapchain_info, &handle));

		// Find out how many textures were generated for the swapchain
		uint32_t surface_count = 0;
		xrEnumerateSwapchainImages(handle, 0, &surface_count, nullptr);

		// We'll want to track our own information about the swapchain, so we can draw stuff onto it! We'll also create
		// a depth buffer for each generated texture here as well with make_surfacedata.
		client::swapchain_t swapchain = {};
		swapchain.width = swapchain_info.width;
		swapchain.height = swapchain_info.height;
		swapchain.handle = handle;

		vector<XrSwapchainImageVulkanKHR> surface_images;
		surface_images.resize(surface_count, { XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR });

		swapchain.surface_data.resize(surface_count);
		xrEnumerateSwapchainImages(swapchain.handle, surface_count, &surface_count, (XrSwapchainImageBaseHeader*)surface_images.data());
		for (uint32_t i = 0; i < surface_count; i++)
		{
			swapchain.surface_data[i] = CreateSurfaceData(renderPlatform, (XrBaseInStructure&)surface_images[i], swapchain_info);
		}
		xr_swapchains.push_back(swapchain);
	};

	//Main view swapchain:
	// Create a swapchain for these viewpoints! A swapchain is a set of texture buffers used for displaying to screen,
	// typically this is a backbuffer and a front buffer, one for rendering data to, and one for displaying on-screen.
	// A note about swapchain image format here! OpenXR doesn't create a concrete image format for the texture, like 
	// DXGI_FORMAT_R8G8B8A8_UNORM. Instead, it switches to the TYPELESS variant of the provided texture format, like 
	// DXGI_FORMAT_R8G8B8A8_TYPELESS. When creating an IVulkanRenderTargetView for the swapchain texture, we must specify
	// a concrete type like DXGI_FORMAT_R8G8B8A8_UNORM, as attempting to create a TYPELESS view will throw errors, so 
	// we do need to store the format separately and remember it later.
	MAIN_SWAPCHAIN = xr_swapchains.size();
	XrSwapchainCreateInfo swapchain_info = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
	swapchain_info.createFlags = 0;
	swapchain_info.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
	swapchain_info.format = swapchain_format;
	swapchain_info.sampleCount = config_view.recommendedSwapchainSampleCount;
	swapchain_info.width = config_view.recommendedImageRectWidth;
	swapchain_info.height = config_view.recommendedImageRectHeight;
	swapchain_info.faceCount = 1;
	swapchain_info.arraySize = view_count;
	swapchain_info.mipCount = 1;
	AddXrSwapchain(swapchain_info);

	//Motion vector swapchains:
	static bool add_motion_swapchains=false;
	if(add_motion_swapchains)
	{
		if (spaceWarpProperties.recommendedMotionVectorImageRectWidth * spaceWarpProperties.recommendedMotionVectorImageRectHeight > 0)
		{
			//Vector
			MOTION_VECTOR_SWAPCHAIN=xr_swapchains.size();
			swapchain_info.createFlags = 0;
			swapchain_info.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
			swapchain_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
			swapchain_info.sampleCount = 1;
			swapchain_info.width = spaceWarpProperties.recommendedMotionVectorImageRectWidth;
			swapchain_info.height = spaceWarpProperties.recommendedMotionVectorImageRectHeight;
			swapchain_info.faceCount = 1;
			swapchain_info.arraySize = view_count;
			swapchain_info.mipCount = 1;
			AddXrSwapchain(swapchain_info);
			
			//Depth
			MOTION_DEPTH_SWAPCHAIN=xr_swapchains.size();
			swapchain_info.usageFlags =XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
			swapchain_info.format = VK_FORMAT_D24_UNORM_S8_UINT;
			AddXrSwapchain(swapchain_info);
		}
	}

	//Quad swapchain:
	static bool add_overlay_swapchain=true;
	if(add_overlay_swapchain)
	{
		OVERLAY_SWAPCHAIN = xr_swapchains.size();
		swapchain_info.createFlags = 0;
		swapchain_info.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
		swapchain_info.format = VK_FORMAT_R8G8B8A8_SRGB;
		swapchain_info.sampleCount = 1;
		swapchain_info.width = 1024;
		swapchain_info.height = 512;
		swapchain_info.faceCount = 1;
		swapchain_info.arraySize = 1;
		swapchain_info.mipCount = 1;
		AddXrSwapchain(swapchain_info);

	#if 1	
		uint32_t img_id;
		XrSwapchainImageAcquireInfo acquireInfo = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO, NULL};
		XR_CHECK(xrAcquireSwapchainImage(xr_swapchains[OVERLAY_SWAPCHAIN].handle, &acquireInfo, &img_id));
		xr_swapchains[OVERLAY_SWAPCHAIN].last_img_id=img_id;
		XrSwapchainImageWaitInfo waitInfo = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, NULL, XR_INFINITE_DURATION};
		XR_CHECK(xrWaitSwapchainImage(xr_swapchains[OVERLAY_SWAPCHAIN].handle, &waitInfo));
		XrSwapchainImageReleaseInfo releaseInfo = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
		XR_CHECK(xrReleaseSwapchainImage(xr_swapchains[OVERLAY_SWAPCHAIN].handle, &releaseInfo));
	#endif
	}

	//Create a CommandBuffer for each image in every swapchain.
	size_t totalIamgeCount = 0;
	for (const auto& xr_swapchain : xr_swapchains)
		totalIamgeCount += xr_swapchain.surface_data.size();

	SIMUL_ASSERT(totalIamgeCount <= 16);
	for (size_t i = 0; i < totalIamgeCount; i++)
	{
		CmdBuffer& commandBuffer = cmdBuffers[i];
		if (!commandBuffer.Init(vulkanDevice->operator VkDevice(), 0))
		{
			std::cerr << "Failed to create command buffer" << std::endl;
		}
	}

	haveXRDevice = true;
	return true;
}

const char* OpenXR::GetOpenXRGraphicsAPIExtensionName() const
{
	return XR_KHR_VULKAN_ENABLE_EXTENSION_NAME;
}

std::set<std::string> OpenXR::GetRequiredExtensions() const
{
	std::set<std::string> str=client::OpenXR::GetRequiredExtensions();
	str.insert(GetOpenXRGraphicsAPIExtensionName());
	str.insert(XR_KHR_VULKAN_ENABLE_EXTENSION_NAME);
	str.insert(XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME);
	str.insert(XR_KHR_ANDROID_THREAD_SETTINGS_EXTENSION_NAME);
	str.insert(XR_FB_SWAPCHAIN_UPDATE_STATE_EXTENSION_NAME);
	str.insert(XR_FB_SWAPCHAIN_UPDATE_STATE_VULKAN_EXTENSION_NAME);
	str.insert(XR_KHR_CONVERT_TIMESPEC_TIME_EXTENSION_NAME);
	return str;
}
#endif