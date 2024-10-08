
#include <vector>
#ifdef _MSC_VER
#define XR_USE_PLATFORM_WIN32
#undef WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif
#include <openxr/openxr.h>
#define XR_USE_TIMESPEC 1
#include <openxr/openxr_platform.h>
#include "OpenXR.h"
#include "fmt/core.h"
#include "Platform/CrossPlatform/Quaterniond.h"
#include "Platform/CrossPlatform/AxesStandard.h"
#include "Platform/Math/Orientation.h"
#include "Platform/Core/StringFunctions.h"
#include "Platform/Math/Orientation.h"
#include "TeleportCore/ErrorHandling.h"
#include "ThisPlatform/StringFunctions.h"
#include "Log.h"
#include "Config.h"
#include "TeleportCore/Threads.h"
#include <regex>
#include "TeleportClient/ClientTime.h"
//#define TIME_UTC 1
#include <ctime>
bool timeInitialized = false;
long long offset_xr_to_client_ns = 0;
#ifndef _MSC_VER
#pragma clang optimize off
#endif
#include "Platform/Math/Pi.h"
static float AngleRange(float a)
{
	if (a < -SIMUL_PI_F)
		a += 2.0f * SIMUL_PI_F;
	if (a > SIMUL_PI_F)
		a -= 2.0f*SIMUL_PI_F;
	return a;
}
const char* teleport::client::stringof(ActionId a)
{
	switch(a)
	{
		case SELECT					: return "SELECT";
		case SHOW_MENU				: return "SHOW_MENU";
		case SYSTEM					: return "SYSTEM";
		case A						: return "A";
		case B						: return "B";
		case X						: return "X";
		case Y						: return "Y";
		case HEAD_POSE				: return "HEAD_POSE";
		case LEFT_TRIGGER			: return "LEFT_TRIGGER";
		case RIGHT_TRIGGER			: return "RIGHT_TRIGGER";
		case LEFT_SQUEEZE			: return "LEFT_SQUEEZE";
		case RIGHT_SQUEEZE			: return "RIGHT_SQUEEZE";
		case LEFT_GRIP_POSE			: return "LEFT_GRIP_POSE";
		case RIGHT_GRIP_POSE		: return "RIGHT_GRIP_POSE";
		case LEFT_AIM_POSE			: return "LEFT_AIM_POSE";
		case RIGHT_AIM_POSE			: return "RIGHT_AIM_POSE";
		case LEFT_STICK_X			: return "LEFT_STICK_X";
		case RIGHT_STICK_X			: return "RIGHT_STICK_X";
		case LEFT_STICK_Y			: return "LEFT_STICK_Y";
		case RIGHT_STICK_Y			: return "RIGHT_STICK_Y";
		case LEFT_HAPTIC			: return "LEFT_HAPTIC";
		case RIGHT_HAPTIC			: return "RIGHT_HAPTIC";
		case MOUSE_LEFT_BUTTON		: return "MOUSE_LEFT_BUTTON";
		case MOUSE_RIGHT_BUTTON		: return "MOUSE_RIGHT_BUTTON";
		case HANDTRACKING_PALM_POSE	:return "HANDTRACKING_PALM_POSE";
		case PINCH					:return "HANDTRACKING_PINCH";
		case GRASP					:return "HANDTRACKING_GRASP";
		case MAX_ACTIONS			: return "MAX_ACTIONS";
		case INVALID:
	default:
		return "INVALID";
	};
}

const char *GetHandTrackingJointName(XrHandJointEXT j)
{
	switch(j)
	{
	case XR_HAND_JOINT_PALM_EXT							:return "palm";
	case XR_HAND_JOINT_WRIST_EXT						:return "wrist";
	case XR_HAND_JOINT_THUMB_METACARPAL_EXT				:return "thumb_metacarpal";
	case XR_HAND_JOINT_THUMB_PROXIMAL_EXT				:return "thumb_proximal";
	case XR_HAND_JOINT_THUMB_DISTAL_EXT					:return "thumb_distal";
	case XR_HAND_JOINT_THUMB_TIP_EXT					:return "thumb_tip";
	case XR_HAND_JOINT_INDEX_METACARPAL_EXT				:return "index_metacarpal";
	case XR_HAND_JOINT_INDEX_PROXIMAL_EXT				:return "index_proximal";
	case XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT			:return "index_intermediate";
	case XR_HAND_JOINT_INDEX_DISTAL_EXT					:return "index_distal";
	case XR_HAND_JOINT_INDEX_TIP_EXT					:return "index_tip";
	case XR_HAND_JOINT_MIDDLE_METACARPAL_EXT			:return "middle_metacarpal";
	case XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT				:return "middle_proximal";
	case XR_HAND_JOINT_MIDDLE_INTERMEDIATE_EXT			:return "middle_intermediate";
	case XR_HAND_JOINT_MIDDLE_DISTAL_EXT				:return "middle_distal";
	case XR_HAND_JOINT_MIDDLE_TIP_EXT					:return "middle_tip";
	case XR_HAND_JOINT_RING_METACARPAL_EXT				:return "ring_metacarpal";
	case XR_HAND_JOINT_RING_PROXIMAL_EXT				:return "ring_proximal";
	case XR_HAND_JOINT_RING_INTERMEDIATE_EXT			:return "ring_intermediate";
	case XR_HAND_JOINT_RING_DISTAL_EXT					:return "ring_distal";
	case XR_HAND_JOINT_RING_TIP_EXT						:return "ring_tip";
	case XR_HAND_JOINT_LITTLE_METACARPAL_EXT			:return "little_metacarpal";
	case XR_HAND_JOINT_LITTLE_PROXIMAL_EXT				:return "little_proximal";
	case XR_HAND_JOINT_LITTLE_INTERMEDIATE_EXT			:return "little_intermediate";
	case XR_HAND_JOINT_LITTLE_DISTAL_EXT				:return "little_distal";
	case XR_HAND_JOINT_LITTLE_TIP_EXT					:return "little_tip";
	default:
	break;
	};
	return "UNKNOWN/INVALID";
};

int GetJointFromName(std::string name)
{
	for (int i=0;i<XR_HAND_JOINT_COUNT_EXT;i++)
	{
		if(name==GetHandTrackingJointName((XrHandJointEXT)i))
		return (int)i;
	}
	return -1;
}

static std::string letters_numbers="abcdefghijklmnopqrstuvwxyz0123456789 ";

bool Match(const std::string& full_string, const std::string& substring)
{
	try
	{
		std::regex regex(substring, std::regex_constants::icase | std::regex::extended);
		std::smatch match;
		if (std::regex_search(full_string, match, regex))
		{
			TELEPORT_CERR << "matches for '" << full_string << "'\n";
			TELEPORT_CERR << "Prefix: '" << match.prefix() << "'\n";
			for (size_t i = 0; i < match.size(); ++i)
				TELEPORT_CERR << i << ": " << match[i] << '\n';
			TELEPORT_CERR << "Suffix: '" << match.suffix() << "\'\n\n";
			return true;
		}
	}
	catch (std::exception&)
	{
		return false;
	}
	catch (...)
	{
		return false;
	}
	return false;
}

const char *GetXRErrorString(XrInstance	xr_instance,XrResult res)
{
	static char str[XR_MAX_RESULT_STRING_SIZE];
	xrResultToString(xr_instance, res, str);
	return str;
}

void teleport::client::ReportError(XrInstance xr_instance, int result,const char *file,int line,const char *failed_cmd)
{
	XrResult res = (XrResult)result;
	const char *str=GetXRErrorString(xr_instance,res);
	std::cerr <<file<<"("<<line<<"): Error: " << (failed_cmd ? failed_cmd:"")<< ": "<<str << std::endl;
}

namespace teleport
{
	namespace client
	{
		MAKE_TO_STRING_FUNC(XrReferenceSpaceType);
		MAKE_TO_STRING_FUNC(XrViewConfigurationType);
		MAKE_TO_STRING_FUNC(XrEnvironmentBlendMode);
		MAKE_TO_STRING_FUNC(XrSessionState);
		MAKE_TO_STRING_FUNC(XrResult);
		MAKE_TO_STRING_FUNC(XrFormFactor);
	}
}

using namespace std;
using namespace platform;
using namespace teleport;
using namespace client;

static std::vector<XrPath> subActionPaths;
const XrPosef	xr_pose_identity = { {0,0,0,1}, {0,0,0} };

static inline XrVector3f XrVector3f_ScalarMultiply(const XrVector3f v, float scale) {
    XrVector3f u;
    u.x = v.x * scale;
    u.y = v.y * scale;
    u.z = v.z * scale;
    return u;
}

static inline XrQuaternionf XrQuaternionf_Multiply(const XrQuaternionf a, const XrQuaternionf b) {
    XrQuaternionf c;
    c.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
    c.y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x;
    c.z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w;
    c.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
    return c;
}
static inline XrVector3f XrVector3f_Add(const XrVector3f u, const XrVector3f v) {
    XrVector3f w;
    w.x = u.x + v.x;
    w.y = u.y + v.y;
    w.z = u.z + v.z;
    return w;
}
static inline XrQuaternionf XrQuaternionf_Inverse(const XrQuaternionf q) {
    XrQuaternionf r;
    r.x = -q.x;
    r.y = -q.y;
    r.z = -q.z;
    r.w = q.w;
    return r;
}
static inline XrVector3f XrQuaternionf_Rotate(const XrQuaternionf a, const XrVector3f v) {
    XrVector3f r;
    XrQuaternionf q = {v.x, v.y, v.z, 0.0f};
    XrQuaternionf aq = XrQuaternionf_Multiply(a, q);
    XrQuaternionf aInv = XrQuaternionf_Inverse(a);
    XrQuaternionf aqaInv = XrQuaternionf_Multiply(aq, aInv);
    r.x = aqaInv.x;
    r.y = aqaInv.y;
    r.z = aqaInv.z;
    return r;
}
static inline XrVector3f XrPosef_Transform(const XrPosef a, const XrVector3f v) {
    XrVector3f r0 = XrQuaternionf_Rotate(a.orientation, v);
    return XrVector3f_Add(r0, a.position);
}
static inline XrPosef XrPosef_Multiply(const XrPosef a, const XrPosef b) {
    XrPosef c;
    c.orientation = XrQuaternionf_Multiply(a.orientation, b.orientation);
    c.position = XrPosef_Transform(a, b.position);
    return c;
}

static inline XrPosef XrPosef_Inverse(const XrPosef a) {
    XrPosef b;
    b.orientation = XrQuaternionf_Inverse(a.orientation);
    b.position = XrQuaternionf_Rotate(b.orientation, XrVector3f_ScalarMultiply(a.position, -1.0f));
    return b;
}


XrPath teleport::client::MakeXrPath(const XrInstance & xr_instance,const char* str)
{
	XrPath path;
	xrStringToPath(xr_instance, str, &path);
	return path;
}

std::string teleport::client::FromXrPath(const XrInstance & xr_instance,XrPath path)
{
	uint32_t strl;
	char text[XR_MAX_PATH_LENGTH];
	XrResult res;
	res=xrPathToString(xr_instance, path, XR_MAX_PATH_LENGTH, &strl, text);
	std::string str;
	if(res==XR_SUCCESS)
	{
		str=text;
	}
	else
	{
		XR_CHECK(res);
	}
	return str;
}
struct XrGraphicsBindingPlatform
{
	XrStructureType			 type;
	const void* XR_MAY_ALIAS	next;
	crossplatform::RenderPlatform* renderPlatform;
} ;

struct XrSwapchainImagePlatform
{
	XrStructureType	  type;
	void* XR_MAY_ALIAS	next;
	crossplatform::Texture* texture;
} ;

void swapchain_destroy(swapchain_t& swapchain)
{
	for (uint32_t i = 0; i < swapchain.surface_data.size(); i++)
	{
		delete swapchain.surface_data[i].depth_view;
		delete swapchain.surface_data[i].target_view;
	}
}

// Function pointers for some OpenXR extension methods we'll use.
PFN_xrCreateDebugUtilsMessengerEXT		ext_xrCreateDebugUtilsMessengerEXT		= nullptr;
PFN_xrDestroyDebugUtilsMessengerEXT		ext_xrDestroyDebugUtilsMessengerEXT		= nullptr;
PFN_xrCreatePassthroughFB				ext_xrCreatePassthroughFB				= nullptr;
PFN_xrDestroyPassthroughFB				ext_xrDestroyPassthroughFB				= nullptr;
PFN_xrPassthroughStartFB				ext_xrPassthroughStartFB				= nullptr;
PFN_xrPassthroughPauseFB				ext_xrPassthroughPauseFB				= nullptr;
PFN_xrCreatePassthroughLayerFB			ext_xrCreatePassthroughLayerFB			= nullptr;
PFN_xrDestroyPassthroughLayerFB			ext_xrDestroyPassthroughLayerFB			= nullptr;
PFN_xrPassthroughLayerPauseFB			ext_xrPassthroughLayerPauseFB			= nullptr;
PFN_xrPassthroughLayerResumeFB			ext_xrPassthroughLayerResumeFB			= nullptr;
PFN_xrPassthroughLayerSetStyleFB		ext_xrPassthroughLayerSetStyleFB		= nullptr;
PFN_xrCreateGeometryInstanceFB			ext_xrCreateGeometryInstanceFB			= nullptr;
PFN_xrDestroyGeometryInstanceFB			ext_xrDestroyGeometryInstanceFB			= nullptr;
PFN_xrGeometryInstanceSetTransformFB	ext_xrGeometryInstanceSetTransformFB	= nullptr;

PFN_xrCreateHandTrackerEXT				ext_xrCreateHandTrackerEXT				= nullptr;
PFN_xrDestroyHandTrackerEXT				ext_xrDestroyHandTrackerEXT				= nullptr;
PFN_xrLocateHandJointsEXT				ext_xrLocateHandJointsEXT				= nullptr;

#ifdef _MSC_VER
PFN_xrConvertWin32PerformanceCounterToTimeKHR ext_xrConvertWin32PerformanceCounterToTimeKHR = nullptr;
#else
PFN_xrConvertTimespecTimeToTimeKHR ext_xrConvertTimespecTimeToTimeKHR = nullptr;
#endif

struct app_transform_buffer_t
{
	float world[16];
	float viewproj[16];
};

static std::vector<const char *> subaction_paths = {"/user/hand/left", "/user/hand/right"};

void InputSession::SetActions(std::initializer_list<ActionInitializer> actions)
{
	inputDeviceStates.resize(2);
	if(actionDefinitions.size()<actions.size())
		actionDefinitions.resize(actions.size());
	actionStates.resize(actionDefinitions.size());
	for (auto& a : actions)
	{
		if(a.actionId>actionDefinitions.size()-1)
		{
			actionDefinitions.resize(a.actionId+1);
			actionStates.resize(actionDefinitions.size());
		}
		auto &def			=actionDefinitions[(uint16_t)a.actionId];
		def.actionId		=a.actionId;
		def.xrActionType	=a.xrActionType;
		def.name			=a.name;
		def.localizedName	=a.localizedName;
		def.subActionPaths=a.subActionPaths;
	}
}

ActionId InputSession::AddAction( const char* name,const char* localizedName,XrActionType xrActionType)
{
	int actionId		=(int)actionDefinitions.size();
	actionDefinitions.resize(actionDefinitions.size()+1);
	actionStates.resize(actionDefinitions.size());
	auto &def			=actionDefinitions[actionId];
	def.actionId		=(ActionId)actionId;
	def.xrActionType	=xrActionType;
	def.name			=name;
	def.localizedName	=localizedName;
	return (ActionId)actionId;
}

void OpenXR::InstanceInit(InputSession &input_session,XrInstance &xr_instance)
{
	for (int i = 0; i < input_session.actionDefinitions.size(); i++)
	{
		ActionId actionId=(ActionId)i;
		auto &def = input_session.actionDefinitions[i];
		XrActionCreateInfo action_info = { XR_TYPE_ACTION_CREATE_INFO };
		action_info.actionType = def.xrActionType;
		strcpy_s(action_info.actionName, XR_MAX_ACTION_NAME_SIZE, def.name.c_str());
		strcpy_s(action_info.localizedActionName, XR_MAX_LOCALIZED_ACTION_NAME_SIZE, def.localizedName.c_str());
		if(def.subActionPaths)
		{
			action_info.countSubactionPaths = (uint32_t)subActionPaths.size();
			action_info.subactionPaths = subActionPaths.data();
		}
		if(actionId!=ActionId::INVALID && def.name.size() && def.localizedName.c_str())
			XR_CHECK(xrCreateAction(input_session.actionSet, &action_info, &def.xrAction));
		def.actionId=actionId;
		def.xrActionType=def.xrActionType;
	}
}

void InputSession::SessionInit(XrInstance xr_instance,XrSession &xr_session)
{
	for (int i=0;i<actionDefinitions.size();i++)
	{
		auto& def = actionDefinitions[i];

		if(def.xrActionType==XR_ACTION_TYPE_POSE_INPUT)
		{
			// Create frames of reference for the pose actions
			XrActionSpaceCreateInfo action_space_info = { XR_TYPE_ACTION_SPACE_CREATE_INFO };
			action_space_info.action			= def.xrAction;
			action_space_info.poseInActionSpace	= xr_pose_identity;
			if(def.subActionPaths)
				action_space_info.subactionPath		=subActionPaths[0];
			XR_CHECK(xrCreateActionSpace(xr_session, &action_space_info, &actionDefinitions[def.actionId].spaces[0]));
			if (def.subActionPaths)
			{
				action_space_info.subactionPath = subActionPaths[1];
				XR_CHECK(xrCreateActionSpace(xr_session, &action_space_info, &actionDefinitions[def.actionId].spaces[1]));
			}
		}
	}
}

void InteractionProfile::Add(XrInstance &xr_instance, std::initializer_list<InteractionProfileBinding> bindings)
{
	xrActionSuggestedBindings.reserve(xrActionSuggestedBindings.size()+bindings.size());
	bindingPaths.reserve(bindingPaths.size()+bindings.size());
	size_t i = 0;
	for (auto elem : bindings)
	{
		XrPath p=0;
		if (xr_instance)
			p = MakeXrPath(xr_instance, elem.complete_path);
		if (elem.action)
		{
			if (p)
				xrActionSuggestedBindings.push_back({elem.action, p});
			bindingPaths.push_back(elem.complete_path);
			i++;
		}
		else
		{
			if (!p)
			{
				TELEPORT_CERR << "InteractionProfile: " << name << ": Failed to create suggested binding as path " << elem.complete_path << " was invalid." << std::endl;
			}
			if (!elem.action)
			{
				//				TELEPORT_CERR<<"InteractionProfile: "<<pr<<": Failed to create suggested binding "<<elem.complete_path<<" as action is null."<<std::endl;
			}
		}
	}
}

void InteractionProfile::Init(XrInstance &xr_instance,const char *pr,std::initializer_list<InteractionProfileBinding> bindings)
{
	name			=pr;
	if(xr_instance)
		profilePath		=MakeXrPath(xr_instance, pr);
	xrActionSuggestedBindings.clear();
	bindingPaths.clear();
	Add(xr_instance,bindings);
}

void InteractionProfile::Add(XrInstance &xr_instance,XrAction action,const char *complete_path,bool virtual_binding)
{
	XrPath p;
	p= MakeXrPath(xr_instance, complete_path);
	if(action&&p)
	{
		xrActionSuggestedBindings.push_back( {action, p});
		bindingPaths.push_back(complete_path);
	}
	else
	{
		if(!p)
		{
			TELEPORT_CERR<<"InteractionProfile: "<<name.c_str()<<": Failed to create suggested binding as path "<<complete_path<<" was invalid."<<std::endl;
		}
		if(!virtual_binding&&!action)
		{
			TELEPORT_CERR<<"InteractionProfile: "<<name.c_str()<<": Failed to create suggested binding "<<complete_path<<" as action is null."<<std::endl;
		}
	}
}

bool OpenXR::CheckXrResult(XrInstance xr_instance,XrResult res)
{
	if(res == XR_SUCCESS)
		return true;
	char str[XR_MAX_RESULT_STRING_SIZE];
	if(xrResultToString(xr_instance, res, str)== XrResult::XR_SUCCESS)
		std::cerr << "CheckXrResult: " << str << std::endl;
	else
		std::cerr << "CheckXrResult: No string found, but result was " << (int)res << std::endl;
	return false;
}

set<std::string> OpenXR::GetRequiredExtensions() const
{
	set<std::string> str;
	str.insert(GetOpenXRGraphicsAPIExtensionName());
	return str;
}

set<std::string> OpenXR::GetOptionalExtensions() const
{
	set<std::string> str;
	str.insert(XR_EXT_DEBUG_UTILS_EXTENSION_NAME);
	str.insert("XR_FB_passthrough");
	str.insert("XR_FB_triangle_mesh");
	str.insert("XR_FB_render_model");
	str.insert("XR_MSFT_controller_model");
	str.insert(XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME);
	str.insert(XR_EXT_HAND_TRACKING_EXTENSION_NAME);
	str.insert(XR_EXT_HAND_INTERACTION_EXTENSION_NAME);
	str.insert(XR_MSFT_HAND_INTERACTION_EXTENSION_NAME);
	str.insert(XR_EXT_PALM_POSE_EXTENSION_NAME);
	str.insert(XR_FB_SPACE_WARP_EXTENSION_NAME);
	
	// Debug utils for extra info
	return str;
}

bool OpenXR::InitInstance()
{
	if(xr_instance)
		return true;
	if(initInstanceThreadState!=ThreadState::INACTIVE)
		return false;
	
	initInstanceThreadState=ThreadState::STARTING;
	bool res=internalInitInstance();
	initInstanceThreadState=ThreadState::INACTIVE;
	return res;
}

bool OpenXR::threadedInitInstance()
{
#ifndef FIX_BROKEN
	std::lock_guard<std::mutex> lock(instanceMutex);
	
	SetThisThreadName("threadedInitInstance");
	// This should only ever block for a short time.
	while(initInstanceThreadState!=ThreadState::STARTING);
	return internalInitInstance();
#else
	return false;
#endif
}

bool OpenXR::IsExtensionEnabled(const std::string &e) const
{
	auto r = std::find(using_extensions.begin(), using_extensions.end(), e);
	return (r != using_extensions.end());
}

bool OpenXR::internalInitInstance()
{
	initInstanceThreadState = ThreadState::RUNNING;
	TELEPORT_CERR<<"initInstanceThreadState = ThreadState::RUNNING\n";
		// OpenXR will fail to initialize if we ask for an extension that OpenXR
		// can't provide! So we need to check our all extensions before 
		// initializing OpenXR with them. Note that even if the extension is 
		// present, it's still possible you may not be able to use it. For 
		// example: the hand tracking extension may be present, but the hand
		// sensor might not be plugged in or turned on. There are often 
		// additional checks that should be made before using certain features!
	set<string> required_extensions;
	set<string> optional_extensions;
	required_extensions = GetRequiredExtensions();
	optional_extensions = GetOptionalExtensions();

	// We'll get a list of extensions that OpenXR provides using this 
	// enumerate pattern. OpenXR often uses a two-call enumeration pattern 
	// where the first call will tell you how much memory to allocate, and
	// the second call will provide you with the actual data!
	uint32_t ext_count = 0;
	XrResult res = xrEnumerateInstanceExtensionProperties(nullptr, 0, &ext_count, nullptr);
	if (res != XR_SUCCESS)
	{
		initInstanceThreadState = ThreadState::FAILED;
		TELEPORT_CERR<<"initInstanceThreadState = ThreadState::FAILED\n";
		return false;
	}
	got_extensions.clear();
	if(!ext_count)
	{
		initInstanceThreadState = ThreadState::FAILED;
		TELEPORT_CERR<<"initInstanceThreadState = ThreadState::FAILED\n";
		return false;
	}
	vector<XrExtensionProperties> xr_instance_extensions(ext_count, { XR_TYPE_EXTENSION_PROPERTIES });
	xrEnumerateInstanceExtensionProperties(nullptr, ext_count, &ext_count, xr_instance_extensions.data());

	for (size_t i = 0; i < xr_instance_extensions.size(); i++)
	{
		const char * got_ext=xr_instance_extensions[i].extensionName;
		got_extensions.insert(got_ext);
	}
	TELEPORT_CERR<<"All extensions:\n";
	for(const auto &x:got_extensions)
	{
		TELEPORT_CERR<<"    "<<x<<"\n";
	}
	for (const auto &e: optional_extensions)
	{
		const char *this_ext=e.c_str();
		auto match_ext=[this_ext](const std::string &ask_ext)
		{
			return strcmp(ask_ext.c_str(), this_ext) == 0;
		};
		if (std::any_of(got_extensions.begin(), got_extensions.end(),match_ext))
		{
			using_extensions.push_back(this_ext);
		}
		else
		{
			TELEPORT_CERR<<"InitInstance: missing optional extension "<<this_ext<<"\n";
		}
	}
	bool missing_required_extension=false;
	for (const auto &e: required_extensions)
	{
		const char *this_ext=e.c_str();
		auto match_ext=[this_ext](const std::string &ask_ext)
		{
			return strcmp(ask_ext.c_str(), this_ext) == 0;
		};
		if (std::any_of(got_extensions.begin(), got_extensions.end(),match_ext))
		{
			using_extensions.push_back(this_ext);
		}
		else
		{
			missing_required_extension=true;
			TELEPORT_CERR<<"InitInstance: missing required extension "<<this_ext<<"\n";
		}
	}
	// If a required extension isn't present, you want to ditch out here!
	// It's possible something like your rendering API might not be provided
	// by the active runtime. APIs like OpenGL don't have universal support.
	if (missing_required_extension)
	{
		initInstanceThreadState=ThreadState::FINISHED;
		TELEPORT_CERR<<"initInstanceThreadState = ThreadState::FAILED\n";
		return false;
	}

	// Initialize OpenXR with the extensions we've found!
	// Convert string to const char*
	std::vector<const char*> using_charptrs(using_extensions.size());
	for(size_t i=0;i<using_extensions.size();i++)
	{
		using_charptrs[i] = using_extensions[i].c_str();
	}
	XrInstanceCreateInfo createInfo = {XR_TYPE_INSTANCE_CREATE_INFO};
	createInfo.enabledExtensionCount = (uint32_t)using_charptrs.size();
	createInfo.enabledExtensionNames = using_charptrs.data();
	createInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
	strcpy_s(createInfo.applicationInfo.applicationName, XR_MAX_APPLICATION_NAME_SIZE,applicationName.c_str());
	
	TELEPORT_CERR<<"xrCreateInstance start"<<std::endl;
	try
	{
		XrResult res=xrCreateInstance(&createInfo, &xr_instance);
		XR_CHECK(res);
	}
	catch(...)
	{
		TELEPORT_CERR<<"xrCreateInstance exception."<<std::endl;
	}
	TELEPORT_CERR<<"xrCreateInstance done"<<std::endl;
	CreateMouseAndKeyboardProfile();
	if(xr_instance)
	{
		subActionPaths.clear();
		for (auto p : subaction_paths)
			subActionPaths.push_back( MakeXrPath(xr_instance, p));
		// Load extension methods that we'll need for this application! There are
		// couple of ways to do this, and this is a fairly manual one. See this
		// file for another way to do it:
		// https://github.com/maluoi/StereoKit/blob/master/StereoKitC/systems/platform/openxr_extensions.h
		xrGetInstanceProcAddr(xr_instance, "xrCreateDebugUtilsMessengerEXT", (PFN_xrVoidFunction*)(&ext_xrCreateDebugUtilsMessengerEXT));
		xrGetInstanceProcAddr(xr_instance, "xrDestroyDebugUtilsMessengerEXT", (PFN_xrVoidFunction*)(&ext_xrDestroyDebugUtilsMessengerEXT));
	
		XrResult result = xrGetInstanceProcAddr(xr_instance, "xrCreatePassthroughFB", (PFN_xrVoidFunction*)(&ext_xrCreatePassthroughFB));
		#define GET_OPENXR_EXT_FUNCTION(name) {\
			XrResult result = xrGetInstanceProcAddr(xr_instance, #name, (PFN_xrVoidFunction*)(&ext_##name));\
			if (XR_FAILED(result)) {\
			  TELEPORT_INTERNAL_CERR("Failed to obtain the function pointer for {0}.\n",#name);\
			}\
		}                                                                                                     \

		GET_OPENXR_EXT_FUNCTION(xrCreateDebugUtilsMessengerEXT);
		GET_OPENXR_EXT_FUNCTION(xrDestroyDebugUtilsMessengerEXT);
		GET_OPENXR_EXT_FUNCTION(xrCreatePassthroughFB);
		GET_OPENXR_EXT_FUNCTION(xrDestroyPassthroughFB);
		GET_OPENXR_EXT_FUNCTION(xrPassthroughStartFB);
		GET_OPENXR_EXT_FUNCTION(xrPassthroughPauseFB);
		GET_OPENXR_EXT_FUNCTION(xrCreatePassthroughLayerFB);
		GET_OPENXR_EXT_FUNCTION(xrDestroyPassthroughLayerFB);
		GET_OPENXR_EXT_FUNCTION(xrPassthroughLayerPauseFB);
		GET_OPENXR_EXT_FUNCTION(xrPassthroughLayerResumeFB);
		GET_OPENXR_EXT_FUNCTION(xrPassthroughLayerSetStyleFB);
		GET_OPENXR_EXT_FUNCTION(xrCreateGeometryInstanceFB);
		GET_OPENXR_EXT_FUNCTION(xrDestroyGeometryInstanceFB	);
		GET_OPENXR_EXT_FUNCTION(xrGeometryInstanceSetTransformFB);
#ifdef _MSC_VER
		GET_OPENXR_EXT_FUNCTION(xrConvertWin32PerformanceCounterToTimeKHR);
#else
		GET_OPENXR_EXT_FUNCTION(xrConvertTimespecTimeToTimeKHR);
#endif
		GET_OPENXR_EXT_FUNCTION(xrCreateHandTrackerEXT);
		GET_OPENXR_EXT_FUNCTION(xrDestroyHandTrackerEXT);
		GET_OPENXR_EXT_FUNCTION(xrLocateHandJointsEXT);
		// Set up a really verbose debug log! Great for dev, but turn this off or
		// down for final builds. WMR doesn't produce much output here, but it
		// may be more useful for other runtimes?
		// Here's some extra information about the message types and severities:
		// https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#debug-message-categorization
	#if TELEPORT_DEBUG_OPENXR
		XrDebugUtilsMessengerCreateInfoEXT debug_info = { XR_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
		debug_info.messageTypes =
			XR_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
			XR_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
			XR_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT |
			XR_DEBUG_UTILS_MESSAGE_TYPE_CONFORMANCE_BIT_EXT;
		debug_info.messageSeverities =
			XR_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
			XR_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
			XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
			XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		debug_info.userCallback = [](XrDebugUtilsMessageSeverityFlagsEXT severity, XrDebugUtilsMessageTypeFlagsEXT types, const XrDebugUtilsMessengerCallbackDataEXT* msg, void* user_data) {
			// Print the debug message we got! There's a bunch more info we could
			// add here too, but this is a pretty good start, and you can always
			// add a breakpoint this line!
			TELEPORT_CERR<<fmt::format("{}: {}\n", msg->functionName, msg->message).c_str()<<std::endl;

			// Output to debug window
			TELEPORT_CERR<<fmt::format( "{}: {}", msg->functionName, msg->message).c_str() << std::endl;

			// Returning XR_TRUE here will force the calling function to fail
			return (XrBool32)XR_FALSE;
		};
		// Start up the debug utils!
		if (ext_xrCreateDebugUtilsMessengerEXT)
			ext_xrCreateDebugUtilsMessengerEXT(xr_instance, &debug_info, &xr_debug);
	#endif

		// Extension classes:
		openXRRenderModel=std::make_shared<OpenXRRenderModel>(xr_instance);
	}
	else
	{
		auto &config = client::Config::GetInstance();
		config.enable_vr=false;
	}
	initInstanceThreadState=ThreadState::FINISHED;
	TELEPORT_CERR<<"initInstanceThreadState = ThreadState::FINISHED\n";

	cylinderOverlayExt = IsExtensionEnabled(XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME);
	overlay.overlayType = cylinderOverlayExt?OverlayType::CYLINDER:OverlayType::QUAD;
	return (xr_instance!=nullptr);
}

void OpenXR::SetRenderPlatform(crossplatform::RenderPlatform *r)
{
	renderPlatform = r;
}

const char* left = "user/hand/left";
const char* right = "user/hand/right";
void OpenXR::CreateMouseAndKeyboardProfile()
{
	MOUSE_KEYBOARD_PROFILE_INDEX = 0;
	if (interactionProfiles.size() <= MOUSE_KEYBOARD_PROFILE_INDEX)
	{
		interactionProfiles.resize(MOUSE_KEYBOARD_PROFILE_INDEX + 1);
		InteractionProfile& mouseAndKeyboard = interactionProfiles[MOUSE_KEYBOARD_PROFILE_INDEX];
		mouseAndKeyboard.Init(xr_instance
			, "/interaction_profiles/simul/mouse_and_keyboard_ext"
			, {
				 {xr_input_session.actionDefinitions[ActionId::MOUSE_LEFT_BUTTON].xrAction	,"/input/mouse/left/click"}
				,{xr_input_session.actionDefinitions[ActionId::MOUSE_RIGHT_BUTTON].xrAction	,"/input/mouse/right/click"}
			});
		// keyboard:
#ifdef _MSC_VER
		for (size_t i = ActionId::MAX_ACTIONS; i < xr_input_session.actionDefinitions.size(); i++)
		{
			std::string path = "/input/keyboard/";
			const auto& def = xr_input_session.actionDefinitions[i];
			path += def.name[0];
			mouseAndKeyboard.Add(xr_instance, xr_input_session.actionDefinitions[i].xrAction, path.c_str(),true);
		}
#endif
		// No need to SuggestBind: OpenXR doesn't know what to do with this!
	}
}

void OpenXR::MakeActions()
{
	XrActionSetCreateInfo actionset_info = { XR_TYPE_ACTION_SET_CREATE_INFO };
	strcpy_s(actionset_info.actionSetName, XR_MAX_ACTION_SET_NAME_SIZE, "teleport_client");
	strcpy_s(actionset_info.localizedActionSetName, XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE, "TeleportClient");
	XR_CHECK(xrCreateActionSet(xr_instance, &actionset_info, &xr_input_session.actionSet));

	InstanceInit(xr_input_session,xr_instance);
	// Bind the actions we just created to specific locations on the Khronos simple_controller
	// definition! These are labeled as 'suggested' because they may be overridden by the runtime
	// preferences. For example, if the runtime allows you to remap buttons, or provides input
	// accessibility settings.
	#define LEFT	"/user/hand/left"
	#define RIGHT	"/user/hand/right"
	interactionProfiles.resize(6);
	
	auto SuggestBind = [this](InteractionProfile &p)
	{
		//The application can call xrSuggestInteractionProfileBindings once per interaction profile that it supports.
		XrInteractionProfileSuggestedBinding suggested_binds = { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
		suggested_binds.interactionProfile = p.profilePath;
		suggested_binds.suggestedBindings = p.xrActionSuggestedBindings.data();
#if TELEPORT_DEBUG_OPENXR
		for(suggested_binds.countSuggestedBindings=1;suggested_binds.countSuggestedBindings<=p.xrActionSuggestedBindings.size();suggested_binds.countSuggestedBindings++)
		{
			XrResult res=xrSuggestInteractionProfileBindings(xr_instance, &suggested_binds);
			if(res!=XR_SUCCESS)
			{
				TELEPORT_CERR<<GetXRErrorString(xr_instance,res)<<" for path "<<FromXrPath(xr_instance,p.xrActionSuggestedBindings[suggested_binds.countSuggestedBindings-1].binding)<<std::endl;
			}
		}
#else
		suggested_binds.countSuggestedBindings = (uint32_t)p.xrActionSuggestedBindings.size();
		XR_CHECK(xrSuggestInteractionProfileBindings(xr_instance, &suggested_binds));
#endif
	};
	InteractionProfile &khrSimpleIP			=interactionProfiles[1];
	InteractionProfile &valveIndexIP		=interactionProfiles[2];
	InteractionProfile &oculusTouch			=interactionProfiles[3];
	InteractionProfile &oculusGo			=interactionProfiles[4];
	InteractionProfile &handInteractionExt	=interactionProfiles[5];
	bool palm_pose_enabled = IsExtensionEnabled(XR_EXT_PALM_POSE_EXTENSION_NAME);
	bool generic_hand_interaction_enabled = IsExtensionEnabled(XR_EXT_HAND_INTERACTION_EXTENSION_NAME);
	bool msft_hand_interaction_enabled = IsExtensionEnabled(XR_MSFT_HAND_INTERACTION_EXTENSION_NAME);
	std::string left_palm_str = LEFT "/input/"s + (palm_pose_enabled ? "palm_ext"s : "grip"s) + "/pose";
	std::string right_palm_str = RIGHT "/input/"s + (palm_pose_enabled ? "palm_ext"s : "grip"s) + "/pose";
	auto AddHandInteractions=[this](InteractionProfile &p)
	{
		p.Add(xr_instance, {
				{xr_input_session.actionDefinitions[ActionId::PINCH].xrAction				,RIGHT "/input/pinch_ext/pose"}
				,{xr_input_session.actionDefinitions[ActionId::PINCH].xrAction				, LEFT "/input/pinch_ext/pose"}
				,{xr_input_session.actionDefinitions[ActionId::GRASP].xrAction				,RIGHT "/input/pinch_ext/pose"}
				,{xr_input_session.actionDefinitions[ActionId::GRASP].xrAction				, LEFT "/input/pinch_ext/pose"}
		});
	};
	khrSimpleIP.Init(xr_instance
			,"/interaction_profiles/khr/simple_controller"
			,{
				 {xr_input_session.actionDefinitions[ActionId::SELECT].xrAction				, LEFT "/input/select/click"}
				,{xr_input_session.actionDefinitions[ActionId::SELECT].xrAction				,RIGHT "/input/select/click"}
				,{xr_input_session.actionDefinitions[ActionId::LEFT_GRIP_POSE].xrAction		, LEFT "/input/grip/pose"}
				,{xr_input_session.actionDefinitions[ActionId::RIGHT_GRIP_POSE].xrAction	,RIGHT "/input/grip/pose"}
				,{xr_input_session.actionDefinitions[ActionId::LEFT_AIM_POSE].xrAction		, LEFT "/input/aim/pose"}
				,{xr_input_session.actionDefinitions[ActionId::RIGHT_AIM_POSE].xrAction		,RIGHT "/input/aim/pose"}
				,{xr_input_session.actionDefinitions[ActionId::LEFT_HAPTIC].xrAction		, LEFT "/output/haptic"}
				,{xr_input_session.actionDefinitions[ActionId::RIGHT_HAPTIC].xrAction		,RIGHT "/output/haptic"}
				,{xr_input_session.actionDefinitions[ActionId::HANDTRACKING_PALM_POSE].xrAction	,right_palm_str.c_str()}
				,{xr_input_session.actionDefinitions[ActionId::HANDTRACKING_PALM_POSE].xrAction	, left_palm_str.c_str()}
			});
	valveIndexIP.Init(xr_instance
		,"/interaction_profiles/valve/index_controller"
		,{
			 {xr_input_session.actionDefinitions[ActionId::LEFT_GRIP_POSE].xrAction		, LEFT "/input/grip/pose"}
			,{xr_input_session.actionDefinitions[ActionId::RIGHT_GRIP_POSE].xrAction	,RIGHT "/input/grip/pose"}
			,{xr_input_session.actionDefinitions[ActionId::LEFT_AIM_POSE].xrAction		, LEFT "/input/aim/pose"}
			,{xr_input_session.actionDefinitions[ActionId::RIGHT_AIM_POSE].xrAction		,RIGHT "/input/aim/pose"}
			,{xr_input_session.actionDefinitions[ActionId::SHOW_MENU].xrAction			, LEFT "/input/b/click" }
			,{xr_input_session.actionDefinitions[ActionId::SHOW_MENU].xrAction			,RIGHT "/input/b/click" }
			,{xr_input_session.actionDefinitions[ActionId::A].xrAction					,RIGHT "/input/a/click" }
			,{xr_input_session.actionDefinitions[ActionId::B].xrAction					,RIGHT "/input/b/click" }
			,{xr_input_session.actionDefinitions[ActionId::X].xrAction					, LEFT "/input/a/click" }		// Note: a and b buttons on both controllers.
			,{xr_input_session.actionDefinitions[ActionId::Y].xrAction					, LEFT "/input/b/click" }
			,{xr_input_session.actionDefinitions[ActionId::LEFT_TRIGGER].xrAction		, LEFT "/input/trigger/value"}
			,{xr_input_session.actionDefinitions[ActionId::RIGHT_TRIGGER].xrAction		,RIGHT "/input/trigger/value"}
			,{xr_input_session.actionDefinitions[ActionId::LEFT_SQUEEZE].xrAction		, LEFT "/input/squeeze/value"}
			,{xr_input_session.actionDefinitions[ActionId::RIGHT_SQUEEZE].xrAction		,RIGHT "/input/squeeze/value"}
			,{xr_input_session.actionDefinitions[ActionId::LEFT_STICK_X].xrAction		, LEFT "/input/thumbstick/x"}
			,{xr_input_session.actionDefinitions[ActionId::RIGHT_STICK_X].xrAction		,RIGHT "/input/thumbstick/x"}
			,{xr_input_session.actionDefinitions[ActionId::LEFT_STICK_Y].xrAction		, LEFT "/input/thumbstick/y"}
			,{xr_input_session.actionDefinitions[ActionId::RIGHT_STICK_Y].xrAction		,RIGHT "/input/thumbstick/y"}
			,{xr_input_session.actionDefinitions[ActionId::HANDTRACKING_PALM_POSE].xrAction	,right_palm_str.c_str()}
			,{xr_input_session.actionDefinitions[ActionId::HANDTRACKING_PALM_POSE].xrAction	, left_palm_str.c_str()}
		});
	oculusTouch.Init(xr_instance
		, "/interaction_profiles/oculus/touch_controller"
		, {
			 {xr_input_session.actionDefinitions[ActionId::LEFT_GRIP_POSE].xrAction			, LEFT "/input/grip/pose"}
			,{xr_input_session.actionDefinitions[ActionId::RIGHT_GRIP_POSE].xrAction		,RIGHT "/input/grip/pose"}
			,{xr_input_session.actionDefinitions[ActionId::LEFT_AIM_POSE].xrAction			, LEFT "/input/aim/pose"}
			,{xr_input_session.actionDefinitions[ActionId::RIGHT_AIM_POSE].xrAction			,RIGHT "/input/aim/pose"}
			,{xr_input_session.actionDefinitions[ActionId::SHOW_MENU].xrAction				, LEFT "/input/menu/click" }
			,{xr_input_session.actionDefinitions[ActionId::A].xrAction						,RIGHT "/input/a/click" }
			,{xr_input_session.actionDefinitions[ActionId::B].xrAction						,RIGHT "/input/b/click" }
			,{xr_input_session.actionDefinitions[ActionId::X].xrAction						, LEFT "/input/x/click" }
			,{xr_input_session.actionDefinitions[ActionId::Y].xrAction						, LEFT "/input/y/click" }
			,{xr_input_session.actionDefinitions[ActionId::LEFT_TRIGGER].xrAction			, LEFT "/input/trigger/value" }
			,{xr_input_session.actionDefinitions[ActionId::RIGHT_TRIGGER].xrAction			,RIGHT "/input/trigger/value" }
			,{xr_input_session.actionDefinitions[ActionId::LEFT_SQUEEZE].xrAction			, LEFT "/input/squeeze/value" }
			,{xr_input_session.actionDefinitions[ActionId::RIGHT_SQUEEZE].xrAction			,RIGHT "/input/squeeze/value" }
			,{xr_input_session.actionDefinitions[ActionId::LEFT_STICK_X].xrAction			, LEFT "/input/thumbstick/x"	}
			,{xr_input_session.actionDefinitions[ActionId::RIGHT_STICK_X].xrAction			,RIGHT "/input/thumbstick/x"}
			,{xr_input_session.actionDefinitions[ActionId::LEFT_STICK_Y].xrAction			, LEFT "/input/thumbstick/y"}
			,{xr_input_session.actionDefinitions[ActionId::RIGHT_STICK_Y].xrAction			,RIGHT "/input/thumbstick/y"}
			,{xr_input_session.actionDefinitions[ActionId::HANDTRACKING_PALM_POSE].xrAction	,right_palm_str.c_str()}
			,{xr_input_session.actionDefinitions[ActionId::HANDTRACKING_PALM_POSE].xrAction	, left_palm_str.c_str()}
		});
	oculusGo.Init(xr_instance, "/interaction_profiles/oculus/go_controller", {
				{xr_input_session.actionDefinitions[ActionId::LEFT_GRIP_POSE].xrAction, LEFT "/input/grip/pose"}
				,{xr_input_session.actionDefinitions[ActionId::RIGHT_GRIP_POSE].xrAction, RIGHT "/input/grip/pose"}
				,{xr_input_session.actionDefinitions[ActionId::LEFT_AIM_POSE].xrAction, LEFT "/input/aim/pose"}
				,{xr_input_session.actionDefinitions[ActionId::RIGHT_AIM_POSE].xrAction, RIGHT "/input/aim/pose"}
				,{xr_input_session.actionDefinitions[ActionId::SHOW_MENU].xrAction, LEFT "/input/menu/click"}
				,{xr_input_session.actionDefinitions[ActionId::SYSTEM].xrAction, LEFT "/input/system/click"}
				,{xr_input_session.actionDefinitions[ActionId::A].xrAction, RIGHT "/input/a/click"}
				,{xr_input_session.actionDefinitions[ActionId::B].xrAction, RIGHT "/input/b/click"}
				,{xr_input_session.actionDefinitions[ActionId::X].xrAction, LEFT "/input/x/click"}
				,{xr_input_session.actionDefinitions[ActionId::Y].xrAction, LEFT "/input/y/click"}
				,{xr_input_session.actionDefinitions[ActionId::LEFT_TRIGGER].xrAction, LEFT "/input/trigger/value"}
				,{xr_input_session.actionDefinitions[ActionId::RIGHT_TRIGGER].xrAction, RIGHT "/input/trigger/value"}
				,{xr_input_session.actionDefinitions[ActionId::LEFT_SQUEEZE].xrAction, LEFT "/input/squeeze/value"}
				,{xr_input_session.actionDefinitions[ActionId::RIGHT_SQUEEZE].xrAction, RIGHT "/input/squeeze/value"}
				,{xr_input_session.actionDefinitions[ActionId::LEFT_STICK_X].xrAction, LEFT "/input/thumbstick/x"}
				,{xr_input_session.actionDefinitions[ActionId::RIGHT_STICK_X].xrAction, RIGHT "/input/thumbstick/x"}
				,{xr_input_session.actionDefinitions[ActionId::LEFT_STICK_Y].xrAction, LEFT "/input/thumbstick/y"}
				,{xr_input_session.actionDefinitions[ActionId::RIGHT_STICK_Y].xrAction, RIGHT "/input/thumbstick/y"}
				,{xr_input_session.actionDefinitions[ActionId::HANDTRACKING_PALM_POSE].xrAction	,right_palm_str.c_str()}
				,{xr_input_session.actionDefinitions[ActionId::HANDTRACKING_PALM_POSE].xrAction	, left_palm_str.c_str()}
			});
	if(generic_hand_interaction_enabled)
	{
		handInteractionExt.Init(xr_instance
		, "/interaction_profiles/ext/hand_interaction_ext"
		, {
			 {xr_input_session.actionDefinitions[ActionId::GRASP].xrAction	, LEFT "/input/grasp_ext/value"}
			 ,{xr_input_session.actionDefinitions[ActionId::GRASP].xrAction	,RIGHT "/input/grasp_ext/value"}
			 ,{xr_input_session.actionDefinitions[ActionId::PINCH].xrAction	, LEFT "/input/pinch_ext/value"}
			 ,{xr_input_session.actionDefinitions[ActionId::PINCH].xrAction	,RIGHT "/input/pinch_ext/value"}
			,{xr_input_session.actionDefinitions[ActionId::HANDTRACKING_PALM_POSE].xrAction	,right_palm_str.c_str()}
			,{xr_input_session.actionDefinitions[ActionId::HANDTRACKING_PALM_POSE].xrAction	, left_palm_str.c_str()}
		});
	}
	else if (msft_hand_interaction_enabled)
	{
		handInteractionExt.Init(xr_instance
		, "/interaction_profiles/microsoft/hand_interaction"
		, {
			 {xr_input_session.actionDefinitions[ActionId::LEFT_GRIP_POSE].xrAction	, LEFT "/input/grip/pose"}
			,{xr_input_session.actionDefinitions[ActionId::RIGHT_GRIP_POSE].xrAction,RIGHT "/input/grip/pose"}
			,{xr_input_session.actionDefinitions[ActionId::LEFT_AIM_POSE].xrAction	, LEFT "/input/aim/pose"}
			,{xr_input_session.actionDefinitions[ActionId::RIGHT_AIM_POSE].xrAction	,RIGHT "/input/aim/pose"}
			,{xr_input_session.actionDefinitions[ActionId::GRASP].xrAction			, LEFT "/input/squeeze/value"}
			,{xr_input_session.actionDefinitions[ActionId::GRASP].xrAction		 	,RIGHT "/input/squeeze/value"}
			,{xr_input_session.actionDefinitions[ActionId::LEFT_SQUEEZE].xrAction	, LEFT "/input/squeeze/value"}
			,{xr_input_session.actionDefinitions[ActionId::RIGHT_SQUEEZE].xrAction	,RIGHT "/input/squeeze/value"}
			//,{xr_input_session.actionDefinitions[ActionId::SHOW_MENU].xrAction	, LEFT "/input/select/value"}
			,{xr_input_session.actionDefinitions[ActionId::LEFT_TRIGGER].xrAction	, LEFT "/input/select/value" }
			,{xr_input_session.actionDefinitions[ActionId::RIGHT_TRIGGER].xrAction	,RIGHT "/input/select/value" }
			,{xr_input_session.actionDefinitions[ActionId::HANDTRACKING_PALM_POSE].xrAction	,right_palm_str.c_str()}
			,{xr_input_session.actionDefinitions[ActionId::HANDTRACKING_PALM_POSE].xrAction	, left_palm_str.c_str()}
		});
		// For hand interaction, SHOW_MENU should only be activated when input/select/value switches AND
		// the hand is facing palm-towards the head position.
	}
	// The following  is supposedly enabled, but not in practice supported:
	if (generic_hand_interaction_enabled)
	{
		AddHandInteractions(khrSimpleIP);
		AddHandInteractions(valveIndexIP);
		AddHandInteractions(oculusTouch);
		AddHandInteractions(handInteractionExt);
		AddHandInteractions(oculusGo);
	}
	SuggestBind(khrSimpleIP);
	SuggestBind(valveIndexIP);
	SuggestBind(oculusTouch);
	SuggestBind(handInteractionExt);
	SuggestBind(oculusGo);
}

void OpenXR::AttachSessionActions()
{
	// Attach the action set we just made to the session
	XrSessionActionSetsAttachInfo attach_info = { XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
	attach_info.countActionSets = 1;
	attach_info.actionSets = &xr_input_session.actionSet;
	XR_CHECK(xrAttachSessionActionSets( xr_session, &attach_info));
	xr_input_session.SessionInit(xr_instance,xr_session);
	if(openXRRenderModel.get())
		openXRRenderModel->SessionInit(xr_session);
	for(auto i:openXRServers)
	{
		BindUnboundPoses(i.first);
	}
}

OpenXR::~OpenXR()
{
	
}

void OpenXR::Tick()
{
	auto& config = client::Config::GetInstance();
	if(config.options.passThrough!=IsPassthroughActive())
	{
		if(!ActivatePassthrough(config.options.passThrough))
		{
			config.options.passThrough=IsPassthroughActive();
		}
	}
	if (config.enable_vr)
	{
		if (initInstanceThreadState == ThreadState::INACTIVE && !xr_instance)
		{
			initInstanceThreadState = ThreadState::STARTING;
			initInstanceThread = std::thread(&OpenXR::threadedInitInstance, this);
		}
		else if (initInstanceThreadState == ThreadState::FINISHED)
		{
			initInstanceThread.join();
			initInstanceThreadState = ThreadState::INACTIVE;
		}
		else if (HaveXRDevice())
		{
			PollEvents();
		}
	}
}

teleport::core::Pose ConvertJointPose(const XrPosef &xrpose)
{
	teleport::core::Pose pose;
	// first convert to the correct scheme.
	vec3 pos_e = crossplatform::ConvertPosition(crossplatform::AxesStandard::OpenGL, crossplatform::AxesStandard::Engineering, *((const vec3 *)&xrpose.position));
	crossplatform::Quaternionf ori_e = crossplatform::ConvertRotation(crossplatform::AxesStandard::OpenGL, crossplatform::AxesStandard::Engineering, *((const vec4 *)&xrpose.orientation));
	
	pose.position = *((vec3 *)&pos_e);
	pose.orientation = *((vec4 *)&ori_e);
	return pose;
}

void OpenXR::PollActions(XrTime predictedTime)
{
	if (xr_session_state != XR_SESSION_STATE_FOCUSED)
		return;
	if (!activeInteractionProfilePaths.size())
	{
		ChangedInteractionProfile();
	}
	for(auto i:openXRServers)
	{
		BindUnboundPoses(i.first);
	}
	
	// Update our action set with up-to-date input data!
	XrActiveActionSet action_set = { };
	action_set.actionSet = xr_input_session.actionSet;
	action_set.subactionPath = XR_NULL_PATH;

	XrActionsSyncInfo sync_info = { XR_TYPE_ACTIONS_SYNC_INFO };
	sync_info.countActiveActionSets = 1;
	sync_info.activeActionSets = &action_set;

	xrSyncActions(xr_session, &sync_info);

	// Now we'll get the current states of our actions, and store them for later use
	for(size_t i=0;i<ActionId::MAX_ACTIONS;i++)
	{
		auto &def=xr_input_session.actionDefinitions[i];
		if(!def.xrAction)
			continue;
		auto &state=xr_input_session.actionStates[i];
		XrActionStateGetInfo get_info = { XR_TYPE_ACTION_STATE_GET_INFO };
		get_info.action = def.xrAction;
		if (def.subActionPaths)
			get_info.subactionPath = subActionPaths[0];
		switch(def.xrActionType)
		{
			case XR_ACTION_TYPE_POSE_INPUT:
			{
				XrActionStatePose pose_state	= { XR_TYPE_ACTION_STATE_POSE };
				get_info.action					= def.xrAction;
				int count=1;
				if(def.subActionPaths)
					count=2;
				for(int j=0;j<count;j++)
				{
					if(def.subActionPaths)
						get_info.subactionPath = subActionPaths[j];
					xrGetActionStatePose(xr_session, &get_info, &pose_state);
					state.subActionStates[j].poseActive = pose_state.isActive;
					if(pose_state.isActive)
					{
						XrSpaceVelocity space_velocity{XR_TYPE_SPACE_VELOCITY};
						XrSpaceLocation space_location{XR_TYPE_SPACE_LOCATION, &space_velocity};
						space_velocity.velocityFlags = XR_SPACE_VELOCITY_LINEAR_VALID_BIT | XR_SPACE_VELOCITY_ANGULAR_VALID_BIT;
						auto space = def.spaces[j];
						auto &subActionState = state.subActionStates[j];
						XrResult res = xrLocateSpace(space, xr_app_space, predictedTime, &space_location);
						if (XR_UNQUALIFIED_SUCCESS(res) &&
							(space_location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0 &&
							(space_location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0)
						{
							subActionState.pose_stageSpace = space_location.pose;
							if (space_velocity.velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT)
								subActionState.velocity_stageSpace = space_velocity.linearVelocity;
							else
								subActionState.velocity_stageSpace = {0, 0, 0};
							if (space_velocity.velocityFlags & XR_SPACE_VELOCITY_ANGULAR_VALID_BIT)
								subActionState.angularVelocity_stageSpace = {0, 0, 0};
						}
						else
						{
							state.subActionStates[j].poseActive = false;
						}
					}
				}
			}
			break;
			case XR_ACTION_TYPE_BOOLEAN_INPUT:
			{
				XrActionStateBoolean bool_state	= { XR_TYPE_ACTION_STATE_BOOLEAN };
				get_info.action = def.xrAction;
				xrGetActionStateBoolean(xr_session, &get_info, &bool_state);
				state.subActionStates[0].isValid = bool_state.isActive;
				if(bool_state.isActive)
				{
					state.subActionStates[0].u32 = bool_state.currentState;
				}
				if (def.subActionPaths)
				{
					get_info.subactionPath = subActionPaths[1];
					xrGetActionStateBoolean(xr_session, &get_info, &bool_state);
					state.subActionStates[1].isValid = bool_state.isActive;
					if (bool_state.isActive)
					{
						state.subActionStates[1].u32 = bool_state.currentState;
					}
				}
			}
			break;
			case XR_ACTION_TYPE_FLOAT_INPUT:
			{
				XrActionStateFloat float_state	= { XR_TYPE_ACTION_STATE_FLOAT };
				get_info.action = def.xrAction;
				xrGetActionStateFloat(xr_session, &get_info, &float_state);
				state.subActionStates[0].isValid = float_state.isActive;
				if (float_state.isActive)
				{
					state.subActionStates[0].f32 = float_state.currentState;
				}
				if (def.subActionPaths)
				{
					get_info.subactionPath = subActionPaths[1];
					xrGetActionStateFloat(xr_session, &get_info, &float_state);
					state.subActionStates[1].isValid = float_state.isActive;
					if (float_state.isActive)
					{
						state.subActionStates[1].f32 = float_state.currentState;
					}
				}
			}
			break;
			case XR_ACTION_TYPE_VECTOR2F_INPUT:
			{
				XrActionStateVector2f vec2_state	= { XR_TYPE_ACTION_STATE_VECTOR2F };
				get_info.action = def.xrAction;
				xrGetActionStateVector2f(xr_session, &get_info, &vec2_state);
				state.subActionStates[0].isValid = vec2_state.isActive;
				if (vec2_state.isActive)
				{
					state.subActionStates[0].vec2f[0]=vec2_state.currentState.x;
					state.subActionStates[0].vec2f[1] = vec2_state.currentState.y;
				}
				if (def.subActionPaths)
				{
					get_info.subactionPath = subActionPaths[1];
					xrGetActionStateVector2f(xr_session, &get_info, &vec2_state);
					state.subActionStates[1].isValid = vec2_state.isActive;
					if (vec2_state.isActive)
					{
						state.subActionStates[1].vec2f[0] = vec2_state.currentState.x;
						state.subActionStates[1].vec2f[1] = vec2_state.currentState.y;
					}
				}
			}
			break;
			default:
			break;
		};
	}
	// process hand interactions.
	// 
        // For each hand, get the pose state if possible.
	if(handTrackingSystemProperties.supportsHandTracking)
	{
		for (int i = 0; i < 2; i++)
		{
			// Specify the subAction Path.
			XrActionStateGetInfo actionStateGetInfo{XR_TYPE_ACTION_STATE_GET_INFO};
			// We pose a single Action, twice - once for each subAction Path.
			actionStateGetInfo.action = xr_input_session.actionDefinitions[HANDTRACKING_PALM_POSE].xrAction;
			actionStateGetInfo.subactionPath = subActionPaths[i];
			XR_CHECK(xrGetActionStatePose(xr_session, &actionStateGetInfo, &xrTrackedHands[i].poseState));
			if (xrTrackedHands[i].poseState.isActive)
			{
				if (!xrTrackedHands[i].hand_tracking_active)
				{
					xrTrackedHands[i].hand_tracking_active = true;
					handTrackingChangedCallback(i, true);
				}
				XrSpaceLocation spaceLocation{XR_TYPE_SPACE_LOCATION};
				XrSpace poseSpace=xr_input_session.actionDefinitions[ActionId::HANDTRACKING_PALM_POSE].spaces[i];
				XrResult res = xrLocateSpace(poseSpace, xr_app_space, predictedTime, &spaceLocation);
				if (XR_UNQUALIFIED_SUCCESS(res) &&
					(spaceLocation.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0 &&
					(spaceLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0)
				{
					xrTrackedHands[i].pose = spaceLocation.pose;
					trackedHands[i].rootPose = ConvertJointPose(spaceLocation.pose);
				}
				bool Unobstructed = true;
				XrHandJointsMotionRangeInfoEXT motionRangeInfo{XR_TYPE_HAND_JOINTS_MOTION_RANGE_INFO_EXT};
				motionRangeInfo.handJointsMotionRange = Unobstructed
															? XR_HAND_JOINTS_MOTION_RANGE_UNOBSTRUCTED_EXT
															: XR_HAND_JOINTS_MOTION_RANGE_CONFORMING_TO_CONTROLLER_EXT;
				XrHandJointsLocateInfoEXT locateInfo{XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT, &motionRangeInfo};
				locateInfo.baseSpace = poseSpace;
				locateInfo.time = predictedTime;

				XrHandJointLocationsEXT locations{XR_TYPE_HAND_JOINT_LOCATIONS_EXT};
				locations.jointCount = (uint32_t)XR_HAND_JOINT_COUNT_EXT;
				locations.jointLocations = xrTrackedHands[i].jointLocations;
				if (!XR_UNQUALIFIED_SUCCESS(ext_xrLocateHandJointsEXT(xrTrackedHands[i].handTracker, &locateInfo, &locations)))
					trackedHands[i].active=false;
				else
				{
					for(uint32_t j=0;j<locations.jointCount;j++)
					{
						trackedHands[i].jointPoses[j]=ConvertJointPose(locations.jointLocations[j].pose);
					}
					trackedHands[i].active = locations.isActive;
				}
				// 
				if(trackedHands[i].active)
				{
					xr_input_session.actionStates[i==0?LEFT_SQUEEZE:RIGHT_SQUEEZE].subActionStates[0].f32=std::min(1.f,std::max(0.f,-2.0f*trackedHands[1].jointPoses[15].orientation.w));
				}
			}
			else
			{
				trackedHands[i].active = false;
				if(xrTrackedHands[i].hand_tracking_active)
				{
					xrTrackedHands[i].hand_tracking_active = false;
					handTrackingChangedCallback(i,false);
				}
			}
		}
	}

	// record the updated pose states for bound nodes:
	for (uint32_t hand = 0; hand < xr_input_session.inputDeviceStates.size(); hand++)
	{
		XrActionStateGetInfo get_info = { XR_TYPE_ACTION_STATE_GET_INFO };
		auto &inputDeviceState =xr_input_session.inputDeviceStates[hand];
	
		XrActionStatePose pose_state = { XR_TYPE_ACTION_STATE_POSE };
		get_info.action				= xr_input_session.actionDefinitions[LEFT_GRIP_POSE+hand].xrAction;
	}
}

const std::vector<teleport::core::Pose> &OpenXR::GetTrackedHandJointPoses(int i)
{
	static std::vector<teleport::core::Pose> emptyPoses;
	if(!trackedHands[i].active)
		return emptyPoses;
	return trackedHands[i].jointPoses;
}

teleport::core::Pose OpenXR::GetTrackedHandRootPose(int i) const
{
	return trackedHands[i].rootPose;
}
void OpenXR::ChangedInteractionProfile()
{
	activeInteractionProfilePaths.clear();
	userHandLeftActiveProfile=0;
	userHandRightActiveProfile = 0;
	if(xr_session)
	{
	// now we are ready to:
		XrInteractionProfileState interactionProfile={XR_TYPE_INTERACTION_PROFILE_STATE,0,0};
		// for each action, what is the binding?
		XR_CHECK(xrGetCurrentInteractionProfile(xr_session,MakeXrPath(xr_instance, "/user/hand/left"),&interactionProfile));
		if (userHandLeftActiveProfile != interactionProfile.interactionProfile)
		{
			std::string pathstr = FromXrPath(xr_instance, interactionProfile.interactionProfile);
			if (interactionProfile.interactionProfile)
				TELEPORT_CERR << " userHandLeftActiveProfile " << pathstr.c_str() << std::endl;
			//if(pathstr=="/interaction_profiles/microsoft/hand_interaction
			userHandLeftActiveProfile = interactionProfile.interactionProfile;
			if (userHandLeftActiveProfile)
				activeInteractionProfilePaths.push_back(userHandLeftActiveProfile);
			if(bindingsChangedCallback)
				bindingsChangedCallback("/user/hand/left"s,pathstr);
		}
		XR_CHECK(xrGetCurrentInteractionProfile(xr_session,MakeXrPath(xr_instance,"/user/hand/right"),&interactionProfile));
		if (userHandRightActiveProfile != interactionProfile.interactionProfile)
		{
			std::string pathstr = FromXrPath(xr_instance, interactionProfile.interactionProfile);
			if (interactionProfile.interactionProfile)
				TELEPORT_CERR << "userHandRightActiveProfile " << pathstr.c_str() << std::endl;
			userHandRightActiveProfile = interactionProfile.interactionProfile;
			if(userHandRightActiveProfile)
				activeInteractionProfilePaths.push_back(userHandRightActiveProfile);
			if (bindingsChangedCallback)
				bindingsChangedCallback("/user/hand/right"s, pathstr);
		}
	}
	// On a desktop machine? Add mouse/keyboard profile.
#ifdef _MSC_VER
	if (MOUSE_KEYBOARD_PROFILE_INDEX >= interactionProfiles.size())
	{
		CreateMouseAndKeyboardProfile();
		InteractionProfile &mouseAndKeyboard = interactionProfiles[MOUSE_KEYBOARD_PROFILE_INDEX];
		activeInteractionProfilePaths.push_back(mouseAndKeyboard.profilePath);
	}
#endif
	for(auto &srv:openXRServers)
	{
		RecordCurrentBindings(srv.first);
	}
}

void OpenXR::RecordCurrentBindings(avs::uid server_uid)
{
	if(!activeInteractionProfilePaths.size())
		return;
	auto server=GetServer(server_uid);
	{
		// TODO: is this the right place to reset the mappings?
		// Put all th mappings back into unbound, to be recalculated later.
		auto &unboundPoses = server->unboundPoses;
		//unboundPoses.clear();
		for (const auto &m : server->nodePoseMappings)
		{
			unboundPoses[m.first].regexPath = m.second.regexPath;
			unboundPoses[m.first].poseOffset = m.second.poseOffset;
		}
		server->nodePoseMappings.clear();
		auto &inputMappings = server->inputMappings;
		if(server_uid!=0)
			inputMappings.clear();
		auto &inputStates = server->inputStates;
		inputStates.clear();
		std::regex re_left_right[2];
		re_left_right[0].assign("/left/", std::regex_constants::icase | std::regex::extended);
		re_left_right[1].assign("/right/", std::regex_constants::icase | std::regex::extended);
		for (const auto &serverInputDef : server->inputDefinitions)
		{
			// Split the regex path by semicolons, allowing multiple definitions.
			std::vector<std::string> strs = platform::core::split(serverInputDef.regexPath, ';');
			for (const auto &str_regex : strs)
			{
				if (str_regex.length() == 0)
					break;
				std::regex re(str_regex, std::regex_constants::icase | std::regex::extended);
				std::cerr << "Trying to bind " << str_regex.c_str() << "\n";
				// which, if any, action should be used to map to this?
				// we match by the bindings.
				// For each action, get the currently bound path.
				int found = 0;
				for (size_t a = 0; a < xr_input_session.actionDefinitions.size(); a++)
				{
					auto &actionDef = xr_input_session.actionDefinitions[a];
					bool matches = false;
					std::string path_str = GetBoundPath(actionDef);
					std::smatch match;
					{
						if (!path_str.length())
							continue;
						// TELEPORT_CERR<<"\tChecking against: "<<path_str.c_str()<<std::endl;
						//  Now we try to match this path to the input serverInputDef.
						if (std::regex_search(path_str, match, re))
						{
							TELEPORT_CERR << "\t\t\tMatches.\n";
							matches = true;
						}
						// else
						//	TELEPORT_CERR<<"\t\t\tX\n";
					}
					if (matches)
					{
						string matching = match.str(0);
						TELEPORT_CERR << "Binding matches: " << str_regex.c_str() << " with " << matching.c_str() << std::endl;
						for (int subActionI = 0; subActionI < (actionDef.subActionPaths ? 2 : 1); subActionI++)
						{
							if (actionDef.subActionPaths)
								if (!std::regex_search(path_str, match, re_left_right[subActionI]))
								{
									continue;
								}
							inputMappings.push_back(InputMapping());
							inputStates.push_back(InputState());
							InputMapping &mapping = inputMappings.back();
							// store the definition.
							mapping.serverInputDefinition = serverInputDef;
							mapping.clientActionId = (ActionId)a;
							mapping.subActionIndex = subActionI;
							found++;

							// If it's in the keyboard range, make sure it's in the boundKeys map.
							if (a >= MAX_ACTIONS && a - MAX_ACTIONS < letters_numbers.size())
							{
								char character = letters_numbers[a - MAX_ACTIONS];
								// keyboard.
								xr_input_session.boundKeys[character] = actionDef.actionId;
							}
						}
					}
				}
				if (found == 0)
				{
					std::string left_path = FromXrPath(xr_instance, userHandLeftActiveProfile);
					std::string right_path = FromXrPath(xr_instance, userHandRightActiveProfile);
					TELEPORT_CERR << "No match found for " << str_regex.c_str() <<" with "<<left_path<< " or "<<right_path<< "\n";
				}
				else
				{
					TELEPORT_CERR << "Found " << found << " matches for " << str_regex.c_str() << "\n";
				}
			}
		}
	}
}

const InteractionProfile *OpenXR::GetActiveBinding(XrPath p) const
{
	for(size_t i=0;i<interactionProfiles.size();i++)
	{
		if(interactionProfiles[i].profilePath==p)
			return &(interactionProfiles[i]);
	}
	return nullptr;
}

void OpenXR::SetFallbackBinding(ActionId actionId,std::string path)
{
	fallbackBindings[actionId].path=path;
}

void OpenXR::SetFallbackPoseState(ActionId actionId,const teleport::core::Pose &pose_worldSpace)
{
	fallbackStates[actionId].pose_worldSpace=pose_worldSpace;
}

void OpenXR::SetFallbackButtonState(ActionId actionId,const bool btn_down)
{
	fallbackStates[actionId].buttonDown=btn_down;
}

void OpenXR::OnMouseButtonPressed(bool bLeftButtonDown, bool bRightButtonDown, bool bMiddleButtonDown, int nMouseWheelDelta)
{
	mouseState.leftButtonDown	|=(bLeftButtonDown ? true : false);
	mouseState.rightButtonDown	|=(bRightButtonDown ? true : false);
	mouseState.middleButtonDown	|=(bMiddleButtonDown ? true : false);
	xr_input_session.actionStates[MOUSE_LEFT_BUTTON].subActionStates[0].u32	=mouseState.leftButtonDown?1:0;
	xr_input_session.actionStates[MOUSE_RIGHT_BUTTON].subActionStates[0].u32 = mouseState.rightButtonDown ? 1 : 0;
}

void OpenXR::OnMouseButtonReleased(bool bLeftButtonReleased, bool bRightButtonReleased, bool bMiddleButtonReleased, int nMouseWheelDelta)
{
	mouseState.leftButtonDown	&=(bLeftButtonReleased ? false : true);
	mouseState.rightButtonDown	&=(bRightButtonReleased ? false : true);
	mouseState.middleButtonDown	&=(bMiddleButtonReleased ? false : true);
	xr_input_session.actionStates[MOUSE_LEFT_BUTTON].subActionStates[0].u32 = mouseState.leftButtonDown ? 1 : 0;
	xr_input_session.actionStates[MOUSE_RIGHT_BUTTON].subActionStates[0].u32 = mouseState.rightButtonDown ? 1 : 0;
}

void OpenXR::OnMouseMove(int xPos, int yPos )
{
}

void OpenXR::OnKeyboard(unsigned wParam, bool bKeyDown)
{
	if(wParam>=(unsigned)'A'&&wParam<=(unsigned)'Z')
	{
		wParam+='a'-'A';
	}
	char k=(char)wParam;
	auto K=xr_input_session.boundKeys.find(k);
	if(K!=xr_input_session.boundKeys.end())
	{
		xr_input_session.actionStates[K->second].subActionStates[0].u32 = bKeyDown ? 1 : 0;
	}
}

std::string OpenXR::GetBoundPath(const ActionDefinition &def) const
{
	for(size_t b=0;b<activeInteractionProfilePaths.size();b++)
	{
		const InteractionProfile *interactionProfile=GetActiveBinding(activeInteractionProfilePaths[b]);
		// For each action, get the currently bound path.
		if(interactionProfile)
		for(size_t a=0;a<interactionProfile->xrActionSuggestedBindings.size();a++)
		{
			auto &binding=interactionProfile->xrActionSuggestedBindings[a];
			if(binding.action==def.xrAction)
			{
				return interactionProfile->bindingPaths[a];
			}
		}
	}
	// No active profiles? Then we may use the fallbacks.
	#ifdef _MSC_VER
	for(auto &binding:fallbackBindings)
	{
		if(binding.first==def.actionId)
			return binding.second.path;
	}
	#endif
	return "";
}
void OpenXRServer::Reset()
{
	inputDefinitions.clear();
	//! Mappings initialized on startup.
	inputMappings.clear();
	nodePoseMappings.clear();
	rootNode=0;
	//! Current states
	inputStates.clear();
	nodePoseStates.clear();
	// temp, filled from nodePoseStates:
	nodePoses.clear();
	//! Temporary - these poses will be bound on the next update.
	unboundPoses.clear();
	inputs.clearEvents();
	framenumber=0;
}

void OpenXR::RemoveServer(avs::uid server_uid)
{
	if(openXRServers.find(server_uid)!=openXRServers.end())
		openXRServers.erase(server_uid);
}

void OpenXR::ResetServer(avs::uid server_uid)
{
	auto i=openXRServers.find(server_uid);
	if(i!=openXRServers.end())
		i->second->Reset();
}

// An InputMapping is created for each InputDefinition that the server has sent.
// It defines which xrActions are linked to which inputs needed by the server->
// The mappings are initialized on connection and can be changed at any time by the server->
// So we have a set of mappings for each currently connected server->
void OpenXR::OnInputsSetupChanged(avs::uid server_uid,const std::vector<teleport::core::InputDefinition>& inputDefinitions_)
{
	auto server = GetServer(server_uid);
	server->inputDefinitions = inputDefinitions_;
	RecordCurrentBindings(server_uid);
}

void OpenXR::SetHardInputMapping(avs::uid server_uid,teleport::core::InputId inputId,teleport::core::InputType inputType,ActionId clientActionId)
{
	auto &inputMappings=GetServer(server_uid)->inputMappings;
	auto &inputStates=GetServer(server_uid)->inputStates;
	inputMappings.push_back(InputMapping());
	inputStates.push_back(InputState());
	InputMapping& mapping = inputMappings.back();
	// store the definition.
	mapping.serverInputDefinition.inputId=inputId;
	mapping.serverInputDefinition.inputType=inputType;
	mapping.clientActionId=clientActionId;
}

void OpenXR::BindUnboundPoses(avs::uid server_uid)
{
	auto &unboundPoses=GetServer(server_uid)->unboundPoses;
	auto &nodePoseMappings=GetServer(server_uid)->nodePoseMappings;
	std::map<avs::uid, NodePoseMapping>::iterator u;
	std::regex re_left_right[2];
	re_left_right[0].assign("/left/", std::regex_constants::icase | std::regex::extended);
	re_left_right[1].assign("/right/", std::regex_constants::icase | std::regex::extended);
	for(u=unboundPoses.begin();u!=unboundPoses.end();)
	{
		avs::uid uid=u->first;
		std::string regexPath=u->second.regexPath;
		std::regex re(regexPath, std::regex_constants::icase | std::regex::extended);
		// which, if any, action should be used to map to this?
		// we match by the bindings.
		// For each action, get the currently bound path.
		bool found_match=false;
		// match for hand tracking must be exact.
		if(regexPath.find("hand_tracking/")==0)
		{
 			vector<string> parts=platform::core::split(regexPath,'/');
			if(parts.size()!=3)
			{
				u++;
				continue;
			}
			int hand=-1;
			if(parts[1]=="left")
				hand=0;
			if(parts[1]=="right")
				hand=1;
			if(hand<0)
			{
				u++;
				continue;
			}
			std::string jointname = parts[2];
			int j=GetJointFromName(jointname);
			if(j>=0&&j<XR_HAND_JOINT_COUNT_EXT)
			{
				nodePoseMappings[uid].actionId = ActionId::INVALID;
				nodePoseMappings[uid].poseOffset = u->second.poseOffset;
				nodePoseMappings[uid].regexPath = u->second.regexPath;
				nodePoseMappings[uid].subActionIndex = hand;
				nodePoseMappings[uid].handJointIndex = j;
				found_match = true;
			}
		}
		else for(size_t a=0;a<ActionId::MAX_ACTIONS;a++)
		{
			auto &actionDef=xr_input_session.actionDefinitions[a];
			if(actionDef.xrActionType!=XR_ACTION_TYPE_POSE_INPUT)
				continue;
			std::string path_str=GetBoundPath(actionDef);
			if(!path_str.length())
				continue;
			// Now we try to match this path to the input def.
			std::smatch match;
			if (std::regex_search(path_str, match, re))
			{
				string matching=match.str(0);
				TELEPORT_CERR<<"Node "<<uid<<" pose Binding matches: "<<regexPath.c_str()<<" with "<<matching.c_str()<<std::endl;
				uint8_t subActionIndex = 0;
				if (actionDef.subActionPaths)
				{
					if (std::regex_search(path_str, match, re_left_right[0]))
					{
						subActionIndex=0;
					}
					if (std::regex_search(path_str, match, re_left_right[1]))
					{
						subActionIndex = 1;
					}
				}
				nodePoseMappings[uid].actionId=(ActionId)a;
				nodePoseMappings[uid].poseOffset=u->second.poseOffset;
				nodePoseMappings[uid].regexPath=u->second.regexPath;
				nodePoseMappings[uid].subActionIndex=subActionIndex;
				found_match=true;
			}
		}
		if(found_match)
		{
			u++;
			unboundPoses.erase(uid);
			continue;
		}
		u++;
	}
	if(unboundPoses.size())
	{
		static short c=1;
		c--;
		if(!c)
		{
			TELEPORT_CERR<<unboundPoses.size()<<" poses remain unbound."<<std::endl;
		}
	}
}

std::shared_ptr<OpenXRServer> OpenXR::GetServer(avs::uid server_uid)
{
	auto f=openXRServers.find(server_uid);
	if(f!=openXRServers.end())
		return f->second;
	openXRServers.emplace(server_uid,std::make_shared<OpenXRServer>());
	return openXRServers[server_uid];
}

void OpenXR::MapNodeToPose(avs::uid server_uid,avs::uid uid,const std::string &regexPath)
{
	teleport::core::Pose poseOffset;
	auto server=GetServer(server_uid);
	if(regexPath=="root"||regexPath=="")
	{
		server->rootNode=uid;
		return;
	}
	auto &unboundPoses=server->unboundPoses;
	unboundPoses[uid].regexPath=regexPath;
	unboundPoses[uid].poseOffset=poseOffset;
	BindUnboundPoses(server_uid);
}

void OpenXR::RemoveNodePoseMapping(avs::uid server_uid,avs::uid uid)
{
	auto server=GetServer(server_uid);
	auto &unboundPoses=server->unboundPoses;
	auto u=unboundPoses.find(uid);
	if(u!=unboundPoses.end())
		unboundPoses.erase(u);
	auto m=server->nodePoseMappings.find(uid);
	if(m!=server->nodePoseMappings.end())
		server->nodePoseMappings.erase(m);

}

void OpenXR::UpdateServerState(avs::uid server_uid,unsigned long long framenumber)
{
	auto server=GetServer(server_uid);
	if(server->framenumber!=framenumber)
	{
		for(auto m:server->nodePoseMappings)
		{
			auto &mapping = m.second;
			auto &definition = xr_input_session.actionDefinitions[mapping.actionId];
			auto &state=server->nodePoseStates[m.first];
			auto space = xr_input_session.actionDefinitions[mapping.actionId].spaces[mapping.subActionIndex];
			auto &subActionState = xr_input_session.actionStates[mapping.actionId].subActionStates[mapping.subActionIndex];
			if (subActionState.poseActive)
			{
				state.pose_footSpace.pose = ConvertGLSpaceToEngineeringSpace(subActionState.pose_stageSpace);
				state.pose_footSpace.velocity = ConvertGLSpaceToEngineeringSpace(subActionState.velocity_stageSpace);
				state.pose_footSpace.angularVelocity = ConvertGLSpaceToEngineeringSpace(subActionState.angularVelocity_stageSpace);
			}
			else
			{
				if (fallbackBindings.find(mapping.actionId) != fallbackBindings.end())
				{
					vec3 p0=*((vec3*)&state.pose_footSpace.pose.position);
					vec3 p1 = *((vec3 *)&fallbackStates[mapping.actionId].pose_worldSpace.position);
					float dt=float(framenumber-server->framenumber)*0.01f;
					vec3 v=(p1-p0)/dt;
					state.pose_footSpace.pose = fallbackStates[mapping.actionId].pose_worldSpace;
					static float r=0.1f;
					vec3 v0=*((vec3*)&state.pose_footSpace.velocity);
					v=v0*(1.f-r)+r*v;
					state.pose_footSpace.velocity			=v;
					state.pose_footSpace.angularVelocity	={0,0,0};
				}
			}
		}
		const float LOWER_HYSTERESIS=0.1f;
		const float UPPER_HYSTERESIS=0.9f;
		server->inputs.clearEvents();
		uint16_t binaryStateIndex=0;
		uint16_t analogueStateIndex=0;
		for(size_t i=0;i<server->inputMappings.size();i++)
		{
			auto &mapping=server->inputMappings[i];
			auto &state=server->inputStates[i];
			const auto &actionState		=xr_input_session.actionStates[mapping.clientActionId].subActionStates[mapping.subActionIndex];
			const auto &actionDefinition=xr_input_session.actionDefinitions[mapping.clientActionId];
			InputState previousState=state;
			state.isValid = actionState.isValid;
			if (!state.isValid)
				continue;
			switch(actionDefinition.xrActionType)
			{
				case XR_ACTION_TYPE_BOOLEAN_INPUT:
					state.uint32=actionState.u32;
				break;
				case XR_ACTION_TYPE_FLOAT_INPUT:
					state.float32=actionState.f32;
				break;
				default:
				break;
			};
			// process as state or as event?
			if((mapping.serverInputDefinition.inputType&teleport::core::InputType::IsEvent)==teleport::core::InputType::IsEvent)
			{
				// don't treat as a change of state if it wasn't valid last-frame.
				if (!previousState.isValid)
					continue;
				// possibilities:
				// float action interpreted as float event:
				if(actionDefinition.xrActionType==XR_ACTION_TYPE_FLOAT_INPUT)
				{
					if((mapping.serverInputDefinition.inputType&teleport::core::InputType::IsFloat)==teleport::core::InputType::IsFloat)
					{
						if(previousState.float32!=state.float32)
							server->inputs.addAnalogueEvent(mapping.serverInputDefinition.inputId,state.float32);
					}
					// float action interpreted as boolean event:
					else if((mapping.serverInputDefinition.inputType&teleport::core::InputType::IsInteger)==teleport::core::InputType::IsInteger)
					{
						if(state.uint32!=0&&state.float32<LOWER_HYSTERESIS)
						{
							server->inputs.addBinaryEvent(mapping.serverInputDefinition.inputId,0);
							state.uint32=0;
						}
						else if(state.uint32==0&&state.float32>UPPER_HYSTERESIS)
						{
							server->inputs.addBinaryEvent(mapping.serverInputDefinition.inputId,1);
							state.uint32=1;
						}
					}
				}
				else if(actionDefinition.xrActionType==XR_ACTION_TYPE_BOOLEAN_INPUT)
				{
					// boolean action as float event:
					if((mapping.serverInputDefinition.inputType&teleport::core::InputType::IsFloat)==teleport::core::InputType::IsFloat)
					{
						if(previousState.uint32!=state.uint32)
						{
							state.float32=state.uint32?1.f:0.f;
							server->inputs.addAnalogueEvent(mapping.serverInputDefinition.inputId,state.float32);
						}
					}
					// boolean action as boolean event:
					else if((mapping.serverInputDefinition.inputType&teleport::core::InputType::IsInteger)==teleport::core::InputType::IsInteger)
					{
						if(previousState.uint32!=state.uint32)
						{
							server->inputs.addBinaryEvent(mapping.serverInputDefinition.inputId,state.uint32);
						}
					}
				}
			}
			else // If it's a state input, always send.
			{
				if((mapping.serverInputDefinition.inputType&teleport::core::InputType::IsFloat)==teleport::core::InputType::IsFloat)
				{
					if(actionDefinition.xrActionType==XR_ACTION_TYPE_BOOLEAN_INPUT)
					{
						state.float32=state.uint32?1.f:0.f;
					}
					server->inputs.setAnalogueState(analogueStateIndex++,state.float32);
				}
				// float action interpreted as boolean event:
				else if((mapping.serverInputDefinition.inputType&teleport::core::InputType::IsInteger)==teleport::core::InputType::IsInteger)
				{
					if(actionDefinition.xrActionType==XR_ACTION_TYPE_FLOAT_INPUT)
					{
						if(state.uint32!=0&&state.float32<LOWER_HYSTERESIS)
						{
							state.uint32=0;
						}
						else if(state.uint32==0&&state.float32>UPPER_HYSTERESIS)
						{
							state.uint32=1;
						}
					}
					server->inputs.setBinaryState(binaryStateIndex++,state.uint32);
				}
			}
		}
		server->framenumber=framenumber;
	}
}

const teleport::core::Input &OpenXR::GetServerInputs(avs::uid server_uid,unsigned long long framenumber)
{
	if(framenumber)
	UpdateServerState(server_uid, framenumber);
	auto server=GetServer(server_uid);
	return server->inputs;
}
const std::vector<InputMapping>& OpenXR::GetServerInputMappings(avs::uid server_uid)
{
	auto server = GetServer(server_uid);
	return server->inputMappings;
}

const std::map<avs::uid, NodePoseMapping >& OpenXR::GetServerNodePoseMappings(avs::uid server_uid)
{
	auto server = GetServer(server_uid);
	return server->nodePoseMappings;
}

avs::uid OpenXR::GetRootNode(avs::uid server_uid)
{
	return GetServer(server_uid)->rootNode;
}

const std::map<avs::uid,teleport::core::PoseDynamic> &OpenXR::GetNodePoses(avs::uid server_uid,unsigned long long framenumber)
{
	const std::map<avs::uid,NodePoseState> &nodePoseStates=GetNodePoseStates(server_uid,framenumber);
	for(const auto &i:nodePoseStates)
	{
		GetServer(server_uid)->nodePoses[i.first].pose=i.second.pose_footSpace.pose;
		GetServer(server_uid)->nodePoses[i.first].velocity=i.second.pose_footSpace.velocity;
		GetServer(server_uid)->nodePoses[i.first].angularVelocity=i.second.pose_footSpace.angularVelocity;
	}
	return GetServer(server_uid)->nodePoses;
}

const std::map<avs::uid,NodePoseState> &OpenXR::GetNodePoseStates(avs::uid server_uid,unsigned long long framenumber)
{
	UpdateServerState(server_uid, framenumber);
	const std::map<avs::uid,NodePoseState> &nodePoseStates=GetServer(server_uid)->nodePoseStates;
	return nodePoseStates;
}

OpenXR::OpenXR(const char *app_name)
{
	applicationName=app_name;
// Define the actions first - we don't need an XR instance for this.
	xr_input_session.SetActions( {
		// Create an action to track the position and orientation of the hands! This is
		// the controller location, or the center of the palms for actual hands.
		 // Create an action for listening to the select action! This is primary trigger
		 // on controllers, and an airtap on HoloLens
		 {ActionId::SELECT				,"select"				,"Select"		,XR_ACTION_TYPE_BOOLEAN_INPUT}
		,{ActionId::SHOW_MENU			,"menu"					,"Menu"			,XR_ACTION_TYPE_BOOLEAN_INPUT}
		,{ActionId::SYSTEM				,"system"				,"System"		,XR_ACTION_TYPE_BOOLEAN_INPUT}
		,{ActionId::A					,"a"					,"A"			,XR_ACTION_TYPE_BOOLEAN_INPUT}
		,{ActionId::B					,"b"					,"B"			,XR_ACTION_TYPE_BOOLEAN_INPUT}
		,{ActionId::X					,"x"					,"X"			,XR_ACTION_TYPE_BOOLEAN_INPUT}
		,{ActionId::Y					,"y"					,"Y"			,XR_ACTION_TYPE_BOOLEAN_INPUT}
		// Action for left controller
		,{ActionId::LEFT_GRIP_POSE		,"left_grip_pose"		,"Left Grip Pose"		,XR_ACTION_TYPE_POSE_INPUT}
		,{ActionId::LEFT_AIM_POSE		,"left_aim_pose"		,"Left Aim Pose"		,XR_ACTION_TYPE_POSE_INPUT}
		,{ActionId::LEFT_TRIGGER		,"left_trigger"			,"Left Trigger"			,XR_ACTION_TYPE_FLOAT_INPUT}
		,{ActionId::LEFT_SQUEEZE		,"left_squeeze"			,"Left Squeeze"			,XR_ACTION_TYPE_FLOAT_INPUT}
		,{ActionId::LEFT_STICK_X		,"left_thumbstick_x"	,"Left Thumbstick X"	,XR_ACTION_TYPE_FLOAT_INPUT}
		,{ActionId::LEFT_STICK_Y		,"left_thumbstick_y"	,"Left Thumbstick Y"	,XR_ACTION_TYPE_FLOAT_INPUT}
		,{ActionId::LEFT_HAPTIC			,"left_haptic"			,"Left Haptic"			,XR_ACTION_TYPE_VIBRATION_OUTPUT}
		// Action for right controller
		,{ActionId::RIGHT_GRIP_POSE		,"right_grip_pose"		,"Right Grip Pose"		,XR_ACTION_TYPE_POSE_INPUT}
		,{ActionId::RIGHT_AIM_POSE		,"right_aim_pose"		,"Right Aim Pose"		,XR_ACTION_TYPE_POSE_INPUT}
		,{ActionId::RIGHT_TRIGGER		,"right_trigger"		,"Right Trigger"		,XR_ACTION_TYPE_FLOAT_INPUT}
		,{ActionId::RIGHT_SQUEEZE		,"right_squeeze"		,"Right Squeeze"		,XR_ACTION_TYPE_FLOAT_INPUT}
		,{ActionId::RIGHT_STICK_X		,"right_thumbstick_x"	,"Right Thumbstick X"	,XR_ACTION_TYPE_FLOAT_INPUT}
		,{ActionId::RIGHT_STICK_Y		,"right_thumbstick_y"	,"Right Thumbstick Y"	,XR_ACTION_TYPE_FLOAT_INPUT}
		,{ActionId::RIGHT_HAPTIC		,"right_haptic"			,"Right Haptic"			,XR_ACTION_TYPE_VIBRATION_OUTPUT}
		// Pseudo-actions for desktop mode:
		,{ActionId::MOUSE_LEFT_BUTTON	,"mouse_left"			,"Left Mouse Button"	,XR_ACTION_TYPE_BOOLEAN_INPUT}
		,{ActionId::MOUSE_RIGHT_BUTTON	,"mouse_right"			,"Right Mouse Button"	,XR_ACTION_TYPE_BOOLEAN_INPUT}
		,{ActionId::HANDTRACKING_PALM_POSE	,"handtracking_palm","Handtracking Palm"	,XR_ACTION_TYPE_POSE_INPUT,true}
		,{ActionId::PINCH				,"handtracking_pinch","Handtracking Pinch"	,XR_ACTION_TYPE_BOOLEAN_INPUT,true}
		,{ActionId::GRASP				,"handtracking_grasp","Handtracking Grasp"	,XR_ACTION_TYPE_BOOLEAN_INPUT,true}
		});
	#ifdef _MSC_VER
	// Add keyboard keys:
	for(size_t i=0;i<letters_numbers.size();i++)
	{
		std::string name;
		name+=letters_numbers[i];
		name+="_key";
		xr_input_session.AddAction(name.c_str(),name.c_str(),XR_ACTION_TYPE_BOOLEAN_INPUT);
	}
	#endif
}

void OpenXR::openxr_poll_predicted(XrTime predicted_time)
{
	if (xr_session_state != XR_SESSION_STATE_FOCUSED)
		return;

	// Update hand position based on the predicted time of when the frame will be rendered! This 
	// should result in a more accurate location, and reduce perceived lag.
	/*for (size_t i = 0; i < 2; i++)
	{
		if (!xr_input_session.inputDeviceStates[i].renderThisDevice)
			continue;
		XrSpaceLocation space_location = { XR_TYPE_SPACE_LOCATION };
		XrResult		res = xrLocateSpace(xr_input_session.actionDefinitions[LEFT_GRIP_POSE+i].spaces[i], xr_app_space, predicted_time, &space_location);
		if (XR_UNQUALIFIED_SUCCESS(res) &&
			(space_location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0 &&
			(space_location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0)
		{
			xr_input_session.actionStates[LEFT_GRIP_POSE+i].pose_stageSpace=( space_location.pose);
		}
	}*/
}

void app_update_predicted()
{
}

double ConvertToClientTimeS(XrInstance instance,XrTime xrTime)
{
	ClientTime &clientTime=ClientTime::GetInstance();
	if (!timeInitialized)
	{
		long long clientTimeNowNs = clientTime.GetTimeNs();
#ifdef _MSC_VER
		LARGE_INTEGER performanceCounter;
		QueryPerformanceCounter(&performanceCounter);
		XrTime xrTimeNowNs;
		// What is the offset between the client time and the xr time?
		// we don't know the Xr time's epoch, but we can convert from Windows perf counter.
		XrResult res=ext_xrConvertWin32PerformanceCounterToTimeKHR(
			instance,
			&performanceCounter,
			&xrTimeNowNs);
		if (!XR_UNQUALIFIED_SUCCESS(res))
		{
			TELEPORT_CERR<<"Failed to initialize time.\n";
		}
		long long txr=(long long)(xrTimeNowNs);
		offset_xr_to_client_ns = clientTimeNowNs - txr;
#else
		timespec timeSp;
		clock_gettime(CLOCK_MONOTONIC, &timeSp);
		XrTime xrTimeNowNs;
		XrResult res = ext_xrConvertTimespecTimeToTimeKHR(
			instance,
			&timeSp,
			&xrTimeNowNs);
		if (!XR_UNQUALIFIED_SUCCESS(res))
		{
			TELEPORT_CERR << "Failed to initialize time.\n";
		}
		long long txr = (long long)(xrTimeNowNs);
		offset_xr_to_client_ns = clientTimeNowNs - txr;
#endif
		timeInitialized = true;
	}
	long long clientTimeNs=(long long)(xrTime)+offset_xr_to_client_ns;
	double clientTimeS=clientTimeNs*1e-9;
	return clientTimeS;
}

void OpenXR::RenderLayerView(crossplatform::GraphicsDeviceContext &deviceContext,XrTime predictedTime, std::vector<XrCompositionLayerProjectionView>& projection_views,
	swapchain_surfdata_t& surface, crossplatform::RenderDelegate& renderDelegate)
{
	if (projection_views.empty())
		return;

	errno=0;

	crossplatform::ViewStruct leftView = CreateViewStructFromXrCompositionLayerProjectionView(projection_views[0], 0, crossplatform::PROJECTION);
	crossplatform::ViewStruct rightView = CreateViewStructFromXrCompositionLayerProjectionView(projection_views[1], 1, crossplatform::PROJECTION);

	//Set the left eye as the GraphicsDeviceContext's ViewStruct, as a backup.
	deviceContext.viewStruct = leftView;
	//Assign the two ViewStruct to the MultiviewGraphicsDeviceContext.
	if (deviceContext.deviceContextType == crossplatform::DeviceContextType::MULTIVIEW_GRAPHICS)
	{
		crossplatform::MultiviewGraphicsDeviceContext& mgdc = *deviceContext.AsMultiviewGraphicsDeviceContext();
		mgdc.viewStructs.resize(2);
		mgdc.viewStructs[0] = leftView;
		mgdc.viewStructs[1] = rightView;
	}

	// Set up where on the render target we want to draw, the view has a 
	const XrRect2Di& rect = projection_views[0].subImage.imageRect;
	crossplatform::Viewport viewport{ (int)rect.offset.x, (int)rect.offset.y, (int)rect.extent.width, (int)rect.extent.height };
	renderPlatform->SetViewports(deviceContext,1,&viewport);

	// Wipe our swapchain color and depth target clean, and then set them up for rendering!
	float clearAlpha=(IsPassthroughActive()?0.0f:1.0f);
	float clear[] = { 0.0f, 0.1f, 0.2f, clearAlpha };
	renderPlatform->ActivateRenderTargets(deviceContext,1, &surface.target_view, surface.depth_view);
	// OpenXR doesn't create a clearable target for Vulkan!
	renderPlatform->Clear(deviceContext, clear);
	if (surface.depth_view)
	{
		surface.depth_view->ClearDepthStencil(deviceContext, 0.0f, 0);
	}
	else
	{
		SIMUL_BREAK("");
	}
	deviceContext.predictedDisplayTimeS = ConvertToClientTimeS(xr_instance,predictedTime);
	
	// And now that we're set up, pass on the rest of our rendering to the application
	renderDelegate(deviceContext);

	renderPlatform->DeactivateRenderTargets(deviceContext);
}

crossplatform::Texture* OpenXR::GetRenderTexture(int index)
{
	if (index < 0 || index >= xr_swapchains.size())
		return nullptr;
	auto sw = xr_swapchains[index];
	if (sw.last_img_id < 0 || sw.last_img_id >= sw.surface_data.size())
		return nullptr;
	return sw.surface_data[sw.last_img_id].target_view;
}

void OpenXR::CreateHandTrackers()
{
	if(ext_xrCreateHandTrackerEXT)
	for (int i = 0; i < 2; i++)
	{
		XrTrackedHand &hand = xrTrackedHands[i];
		XrHandTrackerCreateInfoEXT xrHandTrackerCreateInfo = {XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT};
		xrHandTrackerCreateInfo.hand = i == 0 ? XR_HAND_LEFT_EXT : XR_HAND_RIGHT_EXT;
		xrHandTrackerCreateInfo.handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT;
		XR_CHECK(ext_xrCreateHandTrackerEXT(xr_session, &xrHandTrackerCreateInfo, &hand.handTracker));
		trackedHands[i].jointPoses.resize(XR_HAND_JOINT_COUNT_EXT);
	}
}

void OpenXR::HandleSessionStateChanges( XrSessionState state)
{
	// Session state change is where we can begin and end sessions, as well as find quit messages!
	switch (state)
	{
		case XR_SESSION_STATE_READY:
			{
				XrSessionBeginInfo begin_info = { XR_TYPE_SESSION_BEGIN_INFO };
				CanStartSession();
				if(xrBeginSession(xr_session, &begin_info)==XR_SUCCESS)
				{
					TELEPORT_CERR<<"Beginning OpenXR Session."<<std::endl;
					xr_session_running = true;
					if(sessionChangedCallback)
						sessionChangedCallback(true);
					AttachSessionActions();
					if (handTrackingSystemProperties.supportsHandTracking)
						CreateHandTrackers();
				}
			}
		break;
		case XR_SESSION_STATE_SYNCHRONIZED:
			{
				TELEPORT_CERR<<"OpenXR Session Synchronized."<<std::endl;
			}
			break;
		case XR_SESSION_STATE_FOCUSED:
			{
				TELEPORT_CERR<<"OpenXR Session Focused."<<std::endl;
			}
			break;
		case XR_SESSION_STATE_VISIBLE:
			{
				TELEPORT_CERR<<"OpenXR Session Visible."<<std::endl;
			}
			break;
		case XR_SESSION_STATE_STOPPING:
			{
				xr_session_running = false;
				if(sessionChangedCallback)
					sessionChangedCallback(false);
				EndSession();
			}
			break;
			default:
			break;
	}
	xr_session_state=state;
}

bool OpenXR::RenderLayer( XrTime predictedTime
	, vector<XrCompositionLayerProjectionView>& projection_views,vector<XrCompositionLayerSpaceWarpInfoFB>& spacewarp_views
	, XrCompositionLayerProjection& layer
	, crossplatform::RenderDelegate& renderDelegate)
{
	lastTime=predictedTime;
	// Find the state and location of each viewpoint at the predicted time
	uint32_t view_count = 0;
	XrViewState view_state = { XR_TYPE_VIEW_STATE };
	XrViewLocateInfo locate_info = { XR_TYPE_VIEW_LOCATE_INFO };
	locate_info.viewConfigurationType = app_config_view;
	locate_info.displayTime = predictedTime;
	locate_info.space = xr_app_space;
	XrResult res=xrLocateViews(xr_session, &locate_info, &view_state, (uint32_t)xr_views.size(), &view_count, xr_views.data());
	if(res!=XR_SUCCESS)
	{
		TELEPORT_CERR<<"xrLocateViews failed."<<std::endl;
		return false;
	}
	projection_views.resize(view_count);
	spacewarp_views.resize(view_count);

	// And render to the viewpoints via Multiview!

	// Roderick: There appears to be no consideration at all here of what to do if Multiview is not supported.
	{
		swapchain_t& main_view_xr_swapchain = xr_swapchains[MAIN_SWAPCHAIN];

		// We need to ask which swapchain image to use for rendering! Which one will we get?
		// Who knows! It's up to the runtime to decide.
		uint32_t img_id;
		XrSwapchainImageAcquireInfo acquire_info = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
		XR_CHECK(xrAcquireSwapchainImage(main_view_xr_swapchain.handle, &acquire_info, &img_id));
		main_view_xr_swapchain.last_img_id = img_id;
		// Wait until the image is available to render to. The compositor could still be
		// reading from it.
		XrSwapchainImageWaitInfo wait_info = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
		wait_info.timeout = XR_INFINITE_DURATION;
		XR_CHECK(xrWaitSwapchainImage(main_view_xr_swapchain.handle, &wait_info));

		// Set up our rendering information for the viewpoint we're using right now!
		{
			projection_views[0]								= { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW };
			projection_views[0].pose						= xr_views[0].pose;
			projection_views[0].fov							= xr_views[0].fov;
			projection_views[0].subImage.swapchain			= main_view_xr_swapchain.handle;
			projection_views[0].subImage.imageRect.offset	= { 0, 0 };
			projection_views[0].subImage.imageRect.extent	= { main_view_xr_swapchain.width, main_view_xr_swapchain.height };
			projection_views[0].subImage.imageArrayIndex	= 0;

			projection_views[1]								= { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW };
			projection_views[1].pose						= xr_views[1].pose;
			projection_views[1].fov							= xr_views[1].fov;
			projection_views[1].subImage.swapchain			= main_view_xr_swapchain.handle;
			projection_views[1].subImage.imageRect.offset	= { 0, 0 };
			projection_views[1].subImage.imageRect.extent	= { main_view_xr_swapchain.width, main_view_xr_swapchain.height };
			projection_views[1].subImage.imageArrayIndex	= 1;
		}
		
		crossplatform::GraphicsDeviceContext& deviceContext=GetDeviceContext(MAIN_SWAPCHAIN, img_id);
		deviceContext.setDefaultRenderTargets(nullptr, nullptr, 0, 0, main_view_xr_swapchain.width, main_view_xr_swapchain.height,
			&main_view_xr_swapchain.surface_data[img_id].target_view, 1, main_view_xr_swapchain.surface_data[img_id].depth_view);
		
		deviceContext.renderPlatform = renderPlatform;
		deviceContext.viewStruct.view_id = MAIN_SWAPCHAIN;
		deviceContext.viewStruct.depthTextureStyle = crossplatform::PROJECTION;

		renderPlatform->BeginEvent(deviceContext, "Main View");
		// Call the rendering callback with our view and swapchain info
		RenderLayerView(deviceContext, predictedTime,projection_views, main_view_xr_swapchain.surface_data[img_id], renderDelegate);
		renderPlatform->EndEvent(deviceContext);
	
		FinishDeviceContext(MAIN_SWAPCHAIN, img_id);

		// And tell OpenXR we're done with rendering to this one!
		XrSwapchainImageReleaseInfo release_info = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
		XR_CHECK(xrReleaseSwapchainImage(main_view_xr_swapchain.handle, &release_info));
	}
	
	static bool do_spacewarp=false;
	if(do_spacewarp)
	{
		for (uint32_t i = 0; i < view_count; i++)
		{
			projection_views[i].next = &spacewarp_views[i];
			DoSpaceWarp(projection_views[i],spacewarp_views[i],i);
		}
	}
	
	layer.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
	layer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT | XR_COMPOSITION_LAYER_CORRECT_CHROMATIC_ABERRATION_BIT;
	layer.space = xr_app_space;
	layer.viewCount = (uint32_t)projection_views.size();
	layer.views = projection_views.data();
	return true;
}

void OpenXR::DoSpaceWarp(XrCompositionLayerProjectionView &projection_view,XrCompositionLayerSpaceWarpInfoFB &spacewarp_view,int i)
{
	spacewarp_view.type = XR_TYPE_COMPOSITION_LAYER_SPACE_WARP_INFO_FB;
	spacewarp_view.next = NULL;
	spacewarp_view.layerFlags = 0;
	
	auto& motion_swapchain = xr_swapchains[MOTION_VECTOR_SWAPCHAIN];
	spacewarp_view.motionVectorSubImage.swapchain =motion_swapchain.handle;
	spacewarp_view.motionVectorSubImage.imageRect.offset.x = 0;
	spacewarp_view.motionVectorSubImage.imageRect.offset.y = 0;
	spacewarp_view.motionVectorSubImage.imageRect.extent.width	=motion_swapchain.width;
	spacewarp_view.motionVectorSubImage.imageRect.extent.height	=motion_swapchain.height;
	spacewarp_view.motionVectorSubImage.imageArrayIndex = i;
	
	auto& depth_swapchain = xr_swapchains[MOTION_DEPTH_SWAPCHAIN];
	spacewarp_view.depthSubImage.swapchain = depth_swapchain.handle;
	spacewarp_view.nearZ = 0.1f;
	spacewarp_view.farZ = INFINITY;
	spacewarp_view.depthSubImage.imageRect.offset.x = 0;
	spacewarp_view.depthSubImage.imageRect.offset.y = 0;
	spacewarp_view.depthSubImage.imageRect.extent.width =depth_swapchain.width;
	spacewarp_view.depthSubImage.imageRect.extent.height =depth_swapchain.height;
	spacewarp_view.depthSubImage.imageArrayIndex = i;
	
	// AppSpaceWarp: appSpaceDeltaPose is used to capture appState.CurrentSpace's
	// movement between previous frame and current frame.
	//
	// For example:
	//  * In previous frame, appState.CurrentSpace's application Space (or world
	// space) pose is currentPose.
	//  * In current frame, appState.CurrentSpace's application Space pose is
	//  prevPose.
	//
	// Then appSpaceDeltaPose should be the different of the 2 poses.
	// appSpaceDeltaPose = Inv(prevPose) * currentPose
	//
	// The information will be used in XrRuntime for a couple purposes:
	// 1. Fill in background motion vector: if a pixel on the screen isn't touched
	// by any drawcalls, the pixel will be kept as clear color, which can't be used
	// as correct motion vector. XrRuntime will try to generate it automaticlly, but
	// it need to know if there is any motion driven by the application artificial
	// locomotion, appSpaceDeltaPose provides the information for that.
	// 2. Turn off extrapolation for extreme case: for example if the app had
	// triggered a huge camera movement, we might want to disable frame
	// extrapolation for the frame appSpaceDeltaPose can be used to detect cases
	// like that (eg. teleportation)
	// It is important to make this pose correct by testing camera artificial
	// locomotion rotation with a scene which has foreground rendered object and
	// background only has clear color.
	
	XrPosef PrevFrameXrSpacePoseInWorld		= previousState.XrSpacePoseInWorld;
	XrPosef InvPrevFrameXrSpacePoseInWorld	= XrPosef_Inverse(PrevFrameXrSpacePoseInWorld);
	XrPosef XrSpacePoseInWorld				= state.XrSpacePoseInWorld;
	spacewarp_view.appSpaceDeltaPose		= XrPosef_Multiply(InvPrevFrameXrSpacePoseInWorld, XrSpacePoseInWorld);
	
	// Make debugDeltaPose =1 and rotating the camera with thumbstick,
	// if you looked carefully on the pixels between cube and background color,
	// you can see the artifact of using wrong appSpaceDeltaPose
	int debugDeltaPose = 0;
	/*
	if (!GetSystemPropertyInt("debug.oculus.debugDeltaPose", &debugDeltaPose)) {
		debugDeltaPose = 0; // default value
	}*/
	if (debugDeltaPose > 0) {
		spacewarp_view.appSpaceDeltaPose = xr_pose_identity;
		std::cerr<<"Bad appSpaceDeltaPose: watch carefully on background artifacts"<<std::endl;
	}
	
	spacewarp_view.minDepth = 0.0f;
	spacewarp_view.maxDepth = 1.0f;
	previousState.XrSpacePoseInWorld=state.XrSpacePoseInWorld;
}

void OpenXR::PollEvents()
{
	XrEventDataBuffer event_buffer = { XR_TYPE_EVENT_DATA_BUFFER };
	//XrResult res;
	XrEventDataBaseHeader* baseEventHeader = (XrEventDataBaseHeader*)(&event_buffer);
	baseEventHeader->type = XR_TYPE_EVENT_DATA_BUFFER;
	baseEventHeader->next = NULL;

	// Poll for events
	for (;;)
	{
		XrEventDataBaseHeader* baseEventHeader = (XrEventDataBaseHeader*)(&event_buffer);
		baseEventHeader->type = XR_TYPE_EVENT_DATA_BUFFER;
		baseEventHeader->next = NULL;
		XrResult r;
		r = xrPollEvent(xr_instance, &event_buffer);
		if (r != XR_SUCCESS)
		{
			break;
		}
		switch (baseEventHeader->type)
		{
			case XR_TYPE_EVENT_DATA_EVENTS_LOST:
				TELEPORT_CERR<<"xrPollEvent: received XR_TYPE_EVENT_DATA_EVENTS_LOST event";
				break;
			case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING: {
				const XrEventDataInstanceLossPending* instance_loss_pending_event =
					(XrEventDataInstanceLossPending*)(baseEventHeader);
				TELEPORT_CERR<<
					"xrPollEvent: received XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING event: time "<<
					(instance_loss_pending_event->lossTime)<<std::endl;
			} break;
			case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
				{TELEPORT_CERR<<"xrPollEvent: received XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED event"<<std::endl;
				const XrEventDataInteractionProfileChanged *data =
					(XrEventDataInteractionProfileChanged *)(baseEventHeader);
					ChangedInteractionProfile();
				break;}
			case XR_TYPE_EVENT_DATA_PERF_SETTINGS_EXT: {
				const XrEventDataPerfSettingsEXT* perf_settings_event =
					(XrEventDataPerfSettingsEXT*)(baseEventHeader);
				TELEPORT_CERR<<
					"xrPollEvent: received XR_TYPE_EVENT_DATA_PERF_SETTINGS_EXT event: type "
					<<perf_settings_event->type<<" "
					<<" subdomain "<<perf_settings_event->subDomain<<" "
					<<" fromLevel "<<perf_settings_event->fromLevel<<" "
					<<" toLevel "<<perf_settings_event->toLevel<<std::endl;
			} break;
			case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING: {
				XrEventDataReferenceSpaceChangePending* ref_space_change_event =
					(XrEventDataReferenceSpaceChangePending*)(baseEventHeader);
				TELEPORT_CERR<<
					"xrPollEvent: received XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING event: changed space: "
					<<ref_space_change_event->referenceSpaceType
					<<" for session "<<(void*)ref_space_change_event->session
					<<" time "<<(ref_space_change_event->changeTime)<<std::endl;
			} break;
			case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
				const XrEventDataSessionStateChanged* session_state_changed_event =
					(XrEventDataSessionStateChanged*)(baseEventHeader);
				TELEPORT_CERR<<
					"xrPollEvent: received XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: "
					<<to_string(session_state_changed_event->state)<<
					" for session "<<(void*)session_state_changed_event->session<<
					" time "<<(session_state_changed_event->time)<<std::endl;
					
				HandleSessionStateChanges( session_state_changed_event->state);
			} break;
			default:
				TELEPORT_CERR<<"xrPollEvent: Unknown event"<<std::endl;
				break;
		}
	}
}

bool OpenXR::HaveXRDevice() const
{
	return OpenXR::haveXRDevice;
}

bool OpenXR::IsXRDeviceRendering() const
{
	if(!OpenXR::haveXRDevice)
		return false;
	return (xr_session_state == XR_SESSION_STATE_VISIBLE || xr_session_state == XR_SESSION_STATE_FOCUSED);
}

bool OpenXR::SystemSupportsPassthrough( XrFormFactor formFactor)
{
    XrSystemPassthroughProperties2FB passthroughSystemProperties{XR_TYPE_SYSTEM_PASSTHROUGH_PROPERTIES2_FB};
    XrSystemProperties systemProperties{XR_TYPE_SYSTEM_PROPERTIES, &passthroughSystemProperties};

    XrSystemGetInfo systemGetInfo{XR_TYPE_SYSTEM_GET_INFO};
    systemGetInfo.formFactor = formFactor;

    XrSystemId systemId = XR_NULL_SYSTEM_ID;
	xrGetSystem(xr_instance, &systemGetInfo, &systemId);

    return (passthroughSystemProperties.capabilities & XR_PASSTHROUGH_CAPABILITY_BIT_FB) == XR_PASSTHROUGH_CAPABILITY_BIT_FB;
}

bool OpenXR::ActivatePassthrough(bool on_off)
{
	if(!xr_session)
		return false;
	if(on_off)
	{
		if(!passthroughFeature)
		{
			// Start the feature manually
			XrPassthroughCreateInfoFB xrPassThroughCreateInfoFB = {XR_TYPE_PASSTHROUGH_CREATE_INFO_FB};
			xrPassThroughCreateInfoFB.flags=XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB;
			XrResult result = ext_xrCreatePassthroughFB(xr_session,&xrPassThroughCreateInfoFB,&passthroughFeature);
			if (XR_FAILED(result)) {
			  TELEPORT_INTERNAL_CERR("Failed to start a passthrough feature.\n");
			  return false;
			}
		}
		if(!passthroughLayer)
		{
			// Create and run passthrough layer
			XrPassthroughLayerCreateInfoFB layerCreateInfo = {XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB};
			layerCreateInfo.passthrough = passthroughFeature;
			layerCreateInfo.purpose = XR_PASSTHROUGH_LAYER_PURPOSE_RECONSTRUCTION_FB;
			layerCreateInfo.flags =XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB;

			XrResult result =ext_xrCreatePassthroughLayerFB(xr_session, &layerCreateInfo, &passthroughLayer);
			if (XR_FAILED(result)) {
			  TELEPORT_INTERNAL_CERR("Failed to create and start a passthrough layer");
			  return false;
			}
		}
		return true;
	}
	else
	{
		bool ok=true;
		if(passthroughLayer)
		{
			XrResult result =ext_xrDestroyPassthroughLayerFB(passthroughLayer);
			if (XR_FAILED(result)) {
			  TELEPORT_INTERNAL_CERR("Failed to destroy a passthrough layer");
			  ok= false;
			}
		}
		if(passthroughFeature)
		{
			XrResult result =ext_xrDestroyPassthroughFB(passthroughFeature);
			if (XR_FAILED(result)) {
			  TELEPORT_INTERNAL_CERR("Failed to destroy a passthrough feature");
			  ok= false;
			}
		}
		passthroughLayer=nullptr;
		passthroughFeature=nullptr;
		return ok;
	}
}

bool OpenXR::IsPassthroughActive() const
{
	return passthroughFeature!=nullptr;
}

bool OpenXR::CanStartSession() 
{
	if(IsXRDeviceRendering())
		return false;
	if(xr_session!=XR_NULL_HANDLE)
		return false;
	if(xr_system_id==XR_NULL_SYSTEM_ID)
	{
		if(xr_instance==XR_NULL_HANDLE)
			return false;
		// Request a form factor from the device (HMD, Handheld, etc.)
		// If the device is not on, not connected, or its app is not running, this may fail here:
		XrSystemGetInfo systemInfo = { XR_TYPE_SYSTEM_GET_INFO };
		systemInfo.formFactor = app_config_form;
		if (xrGetSystem(xr_instance, &systemInfo, &xr_system_id)!=XrResult::XR_SUCCESS)
		{
			//TELEPORT_CERR << fmt::format("Failed to Get XR System\n").c_str() << std::endl;
			return false;
		}
		// Check if hand tracking is supported.
		xr_system_properties.next = &handTrackingSystemProperties;
		xrGetSystemProperties(xr_instance,xr_system_id,&xr_system_properties);
		TELEPORT_CERR << "XR System found: " << xr_system_properties.systemName << std::endl;
	}
	if(xr_system_id!=XR_NULL_SYSTEM_ID)
		return true;
	return false;
}

const char * OpenXR::GetSystemName() const
{
	return xr_system_properties.systemName;
}

const teleport::core::Pose& OpenXR::GetHeadPose_StageSpace() const
{
	return headPose_stageSpace;
}

teleport::core::Pose OpenXR::GetActionPose(ActionId actionId,uint8_t subActionIndex) const
{
	teleport::core::Pose pose = ConvertGLSpaceToEngineeringSpace(xr_input_session.actionStates[actionId].subActionStates[subActionIndex].pose_stageSpace);
	return pose;
}

float OpenXR::GetActionFloatState(ActionId actionId, uint8_t subActionIndex) const
{
	float st = xr_input_session.actionStates[actionId].subActionStates[subActionIndex].f32;
	return st;
}

typedef union {
    XrCompositionLayerProjection Projection;
	XrCompositionLayerQuad Quad;
	XrCompositionLayerCylinderKHR Cylinder;
} XrCompositionLayer_Union;
XrCompositionLayer_Union layers[3];

teleport::core::Pose OpenXR::ConvertGLSpaceToEngineeringSpace(const XrPosef &xrpose) 
{
	teleport::core::Pose pose;
	// first convert to the correct scheme.
	vec3 pos_e							= crossplatform::ConvertPosition(crossplatform::AxesStandard::OpenGL, crossplatform::AxesStandard::Engineering, *((const vec3*)&xrpose.position));
 	crossplatform::Quaternionf ori_e	= crossplatform::ConvertRotation(crossplatform::AxesStandard::OpenGL, crossplatform::AxesStandard::Engineering, *((const vec4*)&xrpose.orientation));

	pose.position=*((vec3*)&pos_e);
	pose.orientation=*((vec4*)&ori_e);
	return pose;
}

vec3 OpenXR::ConvertGLSpaceToEngineeringSpace(const XrVector3f &d) const
{
	vec3 D							= crossplatform::ConvertPosition(crossplatform::AxesStandard::OpenGL, crossplatform::AxesStandard::Engineering, *((const vec3*)&d));
	return D;
}

mat4 AffineTransformation(vec4 q, vec3 p)
{
	crossplatform::Quaternion<float> rotation = (const float*)&q;
	vec3 Translation = (const float*)&p;
	vec4 VTranslation = { Translation.x,Translation.y,Translation.z,1.0f };
	mat4 M;
	crossplatform::QuaternionToMatrix(M, rotation);
	vec4 row3 = M.M[3];
	row3 = row3 + VTranslation;
	M.M[3][0] = row3.x;
	M.M[3][1] = row3.y;
	M.M[3][2] = row3.z;
	M.M[3][3] = row3.w;
	return M;
}

mat4 MatrixPerspectiveOffCenterRH
(
	float ViewLeft,
	float ViewRight,
	float ViewBottom,
	float ViewTop,
	float NearZ,
	float FarZ
)
{
	float TwoNearZ = NearZ + NearZ;
	float ReciprocalWidth = 1.0f / (ViewRight - ViewLeft);
	float ReciprocalHeight = 1.0f / (ViewTop - ViewBottom);
	float fRange = FarZ / (NearZ - FarZ);

	mat4 M;
	M.M[0][0] = TwoNearZ * ReciprocalWidth;
	M.M[0][1] = 0.0f;
	M.M[0][2] = 0.0f;
	M.M[0][3] = 0.0f;

	M.M[1][0] = 0.0f;
	M.M[1][1] = TwoNearZ * ReciprocalHeight;
	M.M[1][2] = 0.0f;
	M.M[1][3] = 0.0f;

	M.M[2][0] = (ViewLeft + ViewRight) * ReciprocalWidth;
	M.M[2][1] = (ViewTop + ViewBottom) * ReciprocalHeight;
	M.M[2][2] = 0.f;// NearZ / (FarZ - NearZ); //fRange;
	M.M[2][3] = -1.0f;

	M.M[3][0] = 0.0f;
	M.M[3][1] = 0.0f;
	M.M[3][2] = NearZ;// FarZ* NearZ / (FarZ - NearZ); //fRange * NearZ;
	M.M[3][3] = 0.0f;
	return M;
}
mat4 xr_projection(XrFovf fov, float clip_near, float clip_far)
{
	const float left = clip_near * tanf(fov.angleLeft);
	const float right = clip_near * tanf(fov.angleRight);
	const float down = clip_near * tanf(fov.angleDown);
	const float up = clip_near * tanf(fov.angleUp);

	return MatrixPerspectiveOffCenterRH(left, right, down, up, clip_near, clip_far);
}

math::Matrix4x4 OpenXR::CreateViewMatrixFromPose(const teleport::core::Pose& pose)
{
	// global pos/orientation:
	math::SimulOrientation globalOrientation;
	globalOrientation.SetPosition((const float*)&pose.position);
	math::Quaternion q0(3.1415926536f / 2.0f, math::Vector3(-1.f, 0.0f, 0.0f));
	math::Quaternion q1 = (const float*)&pose.orientation;
	auto q_rel = q1 / q0;
	globalOrientation.SetOrientation(q_rel);
	math::Matrix4x4 view = globalOrientation.GetInverseMatrix().RowPointer(0);
	return view;
}

math::Matrix4x4 OpenXR::CreateTransformMatrixFromPose(const teleport::core::Pose& pose)
{
	// global pos/orientation:
	math::SimulOrientation globalOrientation;
	globalOrientation.SetPosition((const float*)&pose.position);
	math::Quaternion q1 = (const float*)&pose.orientation;
	globalOrientation.SetOrientation(q1);
	math::Matrix4x4 view = globalOrientation.GetInverseMatrix().RowPointer(0);
	return view;
}

crossplatform::ViewStruct OpenXR::CreateViewStructFromXrCompositionLayerProjectionView(XrCompositionLayerProjectionView view, int id, crossplatform::DepthTextureStyle depthTextureStyle)
{
	crossplatform::ViewStruct viewStruct;
	viewStruct.view_id = id;
	viewStruct.depthTextureStyle = depthTextureStyle;

	// Set up camera matrices based on OpenXR's predicted viewpoint information
	mat4 proj = xr_projection(view.fov, 0.1f, 200.0f);
	viewStruct.proj = *((const math::Matrix4x4*)&proj);

	teleport::core::Pose pose = ConvertGLSpaceToEngineeringSpace(view.pose);
	viewStruct.view = CreateViewMatrixFromPose(pose);

	viewStruct.Init();
	return viewStruct;
};


void OpenXR::RenderFrame(crossplatform::RenderDelegate &renderDelegate,crossplatform::RenderDelegate &overlayDelegate)
{
	if(!xr_session_running)
		return;
	//  OpenXR does not use the concept of frame indices. Instead,
	// XrWaitFrame returns the predicted display time.
	XrFrameWaitInfo waitFrameInfo = {};
	waitFrameInfo.type = XR_TYPE_FRAME_WAIT_INFO;
	waitFrameInfo.next = NULL;
	// Block until the previous frame is finished displaying, and is ready for another one.
	// Also returns a prediction of when the next frame will be displayed, for use with predicting
	// locations of controllers, viewpoints, etc.
	XrFrameState frame_state = { XR_TYPE_FRAME_STATE };
	XR_CHECK(xrWaitFrame(xr_session, &waitFrameInfo, &frame_state));
	// Must be called before any rendering is done! This can return some interesting flags, like 
	// XR_SESSION_VISIBILITY_UNAVAILABLE, which means we could skip rendering this frame and call
	// xrEndFrame right away.
	XrFrameBeginInfo beginFrameDesc = {};
	beginFrameDesc.type = XR_TYPE_FRAME_BEGIN_INFO;
	beginFrameDesc.next = NULL;
	XR_CHECK(xrBeginFrame(xr_session, &beginFrameDesc));
	int num_layers=0;
	const XrCompositionLayerBaseHeader* layer_ptrs[5] = {};
	vector<XrCompositionLayerProjectionView> projection_views;
	vector<XrCompositionLayerSpaceWarpInfoFB> spacewarp_views;
	XrCompositionLayerPassthroughFB passthroughCompLayer = {XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB};
	if(frame_state.shouldRender)
	{
		if(passthroughLayer)
		{
			passthroughCompLayer.layerHandle = passthroughLayer;
			passthroughCompLayer.flags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
			passthroughCompLayer.space = XR_NULL_HANDLE;
			layer_ptrs[num_layers++] = (XrCompositionLayerBaseHeader*)&passthroughCompLayer;
		}
		// Execute any code that's dependent on the predicted time, such as updating the location of
		// controller models.
		openxr_poll_predicted(frame_state.predictedDisplayTime);
		app_update_predicted();

		PollActions(frame_state.predictedDisplayTime);
		XrSpaceLocation headspace_location = { XR_TYPE_SPACE_LOCATION };
		XrResult res = xrLocateSpace(xr_head_space, xr_app_space, frame_state.predictedDisplayTime, &headspace_location);
		if (XR_UNQUALIFIED_SUCCESS(res) &&
			(headspace_location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0 &&
			(headspace_location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0)
		{
			state.XrSpacePoseInWorld = headspace_location.pose;
			headPose_stageSpace = ConvertGLSpaceToEngineeringSpace(headspace_location.pose);
			crossplatform::Quaternionf q=headPose_stageSpace.orientation;
			vec3 dir=q.RotateVector({0,1.f,0});
			viewAzimuth = AngleRange(atan2f(-dir.x, dir.y));
		}
		// If the session is active, lets render our layer in the compositor!
	
		// Compose the layers for this frame.
		XrCompositionLayerProjection  &layer_proj=layers[0].Projection;
		layer_proj = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
		bool session_active = xr_session_state == XR_SESSION_STATE_VISIBLE || xr_session_state == XR_SESSION_STATE_FOCUSED || xr_session_state == XR_SESSION_STATE_SYNCHRONIZED;
		if (session_active && RenderLayer(frame_state.predictedDisplayTime, projection_views,spacewarp_views, layer_proj, renderDelegate))
		{
			layer_ptrs[num_layers++] = (XrCompositionLayerBaseHeader*)&layer_proj;
		}

		if(add_overlay)
		{
			RenderOverlayLayer(frame_state.predictedDisplayTime,overlayDelegate);

			bool add=false;
			if(overlay.overlayType==OverlayType::CYLINDER)
				add=AddCylinderOverlayLayer(frame_state.predictedDisplayTime, layers[1].Cylinder, 0);
			else
				add=AddQuadOverlayLayer(frame_state.predictedDisplayTime, layers[1].Quad, 0);

			if(add)
			{
				layer_ptrs[num_layers++] = (XrCompositionLayerBaseHeader*)&layers[1];
			}
		}
		EndFrame();
	}
	// We're finished with rendering our layer, so send it off for display!
	XrFrameEndInfo end_info{ XR_TYPE_FRAME_END_INFO };
	end_info.displayTime = frame_state.predictedDisplayTime;
	end_info.environmentBlendMode = xr_environment_blend;
	end_info.layerCount = num_layers;
	end_info.layers = layer_ptrs;
	XR_CHECK(xrEndFrame(xr_session, &end_info));
}
 
bool OpenXR::RenderOverlayLayer(XrTime predictedTime,crossplatform::RenderDelegate &overlayDelegate)
{
	swapchain_t& overlay_xr_swapchain = xr_swapchains[OVERLAY_SWAPCHAIN];
	uint32_t img_id;
	XrSwapchainImageAcquireInfo acquireInfo = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO, NULL};
	XR_CHECK(xrAcquireSwapchainImage(overlay_xr_swapchain.handle, &acquireInfo, &img_id));
	overlay_xr_swapchain.last_img_id = img_id;
	XrSwapchainImageWaitInfo waitInfo = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, NULL, XR_INFINITE_DURATION};
	XR_CHECK(xrWaitSwapchainImage(overlay_xr_swapchain.handle, &waitInfo));
	
	crossplatform::GraphicsDeviceContext& deviceContext=GetDeviceContext(OVERLAY_SWAPCHAIN, img_id);
	deviceContext.setDefaultRenderTargets(nullptr, nullptr, 0, 0, overlay_xr_swapchain.width, overlay_xr_swapchain.height, 
		&overlay_xr_swapchain.surface_data[img_id].target_view, 1, nullptr);
	
	deviceContext.renderPlatform = renderPlatform;
	deviceContext.viewStruct.view_id = OVERLAY_SWAPCHAIN;
	deviceContext.viewStruct.depthTextureStyle = crossplatform::PROJECTION;

	renderPlatform->BeginEvent(deviceContext, "Overlays");
	// Set up where on the render target we want to draw, the view has a 
	crossplatform::Viewport viewport{ (int)0, (int)0, (int)overlay_xr_swapchain.width, (int)overlay_xr_swapchain.height };
	renderPlatform->SetViewports(deviceContext,1,&viewport);
	renderPlatform->ActivateRenderTargets(deviceContext, 1, &overlay_xr_swapchain.surface_data[img_id].target_view, overlay_xr_swapchain.surface_data[img_id].depth_view);
	
	// Wipe our swapchain color and depth target clean, and then set them up for rendering!
	static float clear[] = { 0.0f, 0.0f, 0.0f, 0.0f };
	renderPlatform->Clear(deviceContext, clear);
	overlayDelegate(deviceContext);

	renderPlatform->DeactivateRenderTargets(deviceContext);
	renderPlatform->EndEvent(deviceContext);

	FinishDeviceContext(OVERLAY_SWAPCHAIN, img_id);
	XrSwapchainImageReleaseInfo releaseInfo = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
	XR_CHECK(xrReleaseSwapchainImage(overlay_xr_swapchain.handle, &releaseInfo));

	return true;
}
static inline XrQuaternionf XrQuaternionf_Identity() {
    XrQuaternionf r;
    r.x = r.y = r.z = 0.0;
    r.w = 1.0f;
    return r;
}

static inline float XrVector3f_LengthSquared(const XrVector3f v) {
    return v.x*v.x+v.y*v.y+v.z*v.z;
}

static inline float XrVector3f_Length(const XrVector3f v) {
    return sqrtf(XrVector3f_LengthSquared(v));
}

static inline XrVector3f XrVector3f_Normalized(const XrVector3f v) {
    float rcpLen = 1.0f / XrVector3f_Length(v);
    return XrVector3f_ScalarMultiply(v, rcpLen);
}
static  XrQuaternionf XrQuaternionf_CreateFromVectorAngle(
    const XrVector3f axis,
    const float angle) {
    XrQuaternionf r;
    if (XrVector3f_LengthSquared(axis) == 0.0f) {
        return XrQuaternionf_Identity();
    }

    XrVector3f unitAxis = XrVector3f_Normalized(axis);
    float sinHalfAngle = sinf(angle * 0.5f);

    r.w = cosf(angle * 0.5f);
    r.x = unitAxis.x * sinHalfAngle;
    r.y = unitAxis.y * sinHalfAngle;
    r.z = unitAxis.z * sinHalfAngle;
    return r;
}

void OpenXR::UpdateOverlayPosition()
{
	// Build the layer
	const float arc= (SIMUL_PI_F / 4.0f);
	// nearest 90 degrees:
	int A = int(std::nearbyint(viewAzimuth / arc));
	if (overlayAzimuth > 10.f)
	{
		overlayAzimuth = targetOverlayAzimuth = float(A) * arc;
	}
	else
	{
		azimuthDiffAngle = AngleRange(targetOverlayAzimuth - overlayAzimuth);
		overlayAzimuth += 0.005f * azimuthDiffAngle;
		overlayAzimuth = AngleRange(overlayAzimuth);
		static float hysteresis=0.95f;
		if (fabs(AngleRange(viewAzimuth - targetOverlayAzimuth))> (arc * hysteresis))
		{
			targetOverlayAzimuth = float(A) * arc;
		}
	}
}

bool OpenXR::AddCylinderOverlayLayer(XrTime predictedTime, XrCompositionLayerCylinderKHR &cylinder_layer, int i)
{
	UpdateOverlayPosition();
	cylinder_layer.type = XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR;
	cylinder_layer.next = NULL;
	cylinder_layer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
	cylinder_layer.space = xr_app_space;
	cylinder_layer.eyeVisibility = XR_EYE_VISIBILITY_BOTH; 
	memset(&cylinder_layer.subImage, 0, sizeof(XrSwapchainSubImage));
	cylinder_layer.subImage.swapchain = xr_swapchains[OVERLAY_SWAPCHAIN].handle;
	cylinder_layer.subImage.imageRect.offset.x = 0;
	cylinder_layer.subImage.imageRect.offset.y = 0;
	cylinder_layer.subImage.imageRect.extent.width = xr_swapchains[OVERLAY_SWAPCHAIN].width;
	cylinder_layer.subImage.imageRect.extent.height = xr_swapchains[OVERLAY_SWAPCHAIN].height;
	cylinder_layer.subImage.imageArrayIndex = 0; // AJR: Only composite the top layer image array, as the overlay quad will be rendered in 3D space.

	overlay.pose.orientation = XrQuaternionf_CreateFromVectorAngle({0, 1.0f, 0}, overlayAzimuth);
	overlay.pose.position = state.XrSpacePoseInWorld.position;
	overlay.pose.position.y=overlay.centreHeight;
	cylinder_layer.pose = overlay.pose;
	cylinder_layer.radius=overlay.radius;
	// width in radians
	cylinder_layer.centralAngle = overlay.angularSpanRadians;
	cylinder_layer.aspectRatio = float(cylinder_layer.subImage.imageRect.extent.width) / float(cylinder_layer.subImage.imageRect.extent.height);
	
	return true;
}

bool OpenXR::AddQuadOverlayLayer(XrTime predictedTime,XrCompositionLayerQuad &quad_layer,int i)
{
	UpdateOverlayPosition();
	// Build the quad layer
    quad_layer.type = XR_TYPE_COMPOSITION_LAYER_QUAD;
    quad_layer.next = NULL;
    quad_layer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    quad_layer.space = xr_app_space;
	quad_layer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    memset(&quad_layer.subImage, 0, sizeof(XrSwapchainSubImage));
    quad_layer.subImage.swapchain = xr_swapchains[OVERLAY_SWAPCHAIN].handle;
    quad_layer.subImage.imageRect.offset.x = 0;
    quad_layer.subImage.imageRect.offset.y = 0;
	quad_layer.subImage.imageRect.extent.width = xr_swapchains[OVERLAY_SWAPCHAIN].width;
	quad_layer.subImage.imageRect.extent.height = xr_swapchains[OVERLAY_SWAPCHAIN].height;
	quad_layer.subImage.imageArrayIndex = 0; //AJR: Only composite the top layer image array, as the overlay quad will be rendered in 3D space.

    overlay.pose.orientation = XrQuaternionf_CreateFromVectorAngle({0, 1.0f, 0}, overlayAzimuth);
	overlay.pose.position = {-sin(overlayAzimuth) * overlay.radius, overlay.centreHeight, -cos(overlayAzimuth) * overlay.radius};
    quad_layer.pose = overlay.pose;
	float w = overlay.radius * tan(overlay.angularSpanRadians);
	float h = w * float(quad_layer.subImage.imageRect.extent.height) / float(quad_layer.subImage.imageRect.extent.width);
	overlay.size = {w,h};
	quad_layer.size = overlay.size;
	return true;
}

void OpenXR::Shutdown()
{
	while(initInstanceThreadState==ThreadState::RUNNING)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	if(initInstanceThread.joinable())
		initInstanceThread.join();
	EndSession();
	if (xr_debug != XR_NULL_HANDLE)
		ext_xrDestroyDebugUtilsMessengerEXT(xr_debug);
	openXRRenderModel.reset();
	if (xr_instance != XR_NULL_HANDLE)
		xrDestroyInstance(xr_instance);
}

void OpenXR::EndSession()
{
	haveXRDevice = false;
	// We used a graphics API to initialize the swapchain data, so we'll
	// give it a chance to release anythig here!
	for (int32_t i = 0; i < xr_swapchains.size(); i++)
	{
		xrDestroySwapchain(xr_swapchains[i].handle);
		swapchain_destroy(xr_swapchains[i]);
	}
	xr_swapchains.clear();

	// Release all the other OpenXR resources that we've created!
	// What gets allocated, must get deallocated!
	for(int i=0;i<ActionId::MAX_ACTIONS;i++)
	{
		if (xr_input_session.actionDefinitions[i].spaces[0] != XR_NULL_HANDLE)
			xrDestroySpace(xr_input_session.actionDefinitions[i].spaces[0]);
		if (xr_input_session.actionDefinitions[i].spaces[1] != XR_NULL_HANDLE)
			xrDestroySpace(xr_input_session.actionDefinitions[i].spaces[1]);
	}
	if (xr_input_session.actionSet != XR_NULL_HANDLE)
	{
		xrDestroyActionSet(xr_input_session.actionSet);
	}
	if (xr_app_space != XR_NULL_HANDLE)
		xrDestroySpace(xr_app_space);
	if(openXRRenderModel.get())
		openXRRenderModel->SessionEnd();
	if (xr_session != XR_NULL_HANDLE)
		xrDestroySession(xr_session);
	if(ext_xrDestroyHandTrackerEXT)
	for (int i = 0; i < 2; i++)
	{
		ext_xrDestroyHandTrackerEXT(xrTrackedHands[i].handTracker);
	}
	xr_session=0;
}


static const char *stringOf(XrSessionState s)
{
	switch(s)
	{
		case XR_SESSION_STATE_UNKNOWN:			
			return "UNKNOWN";
		case XR_SESSION_STATE_IDLE:				
			return "IDLE";
		case XR_SESSION_STATE_READY:			
			return "READY";
		case XR_SESSION_STATE_SYNCHRONIZED:
			return "SYNCHRONIZED";
		case XR_SESSION_STATE_VISIBLE:
			return "VISIBLE";
		case XR_SESSION_STATE_FOCUSED:			
			return "FOCUSED";
		case XR_SESSION_STATE_STOPPING:
			return "STOPPING";
		case XR_SESSION_STATE_LOSS_PENDING:
			return "LOSS_PENDING";
		case XR_SESSION_STATE_EXITING:
			return "EXITING";
		default:
			return "INVALID";
	}
}

const std::string &OpenXR::GetDebugString() const
{
	static std::string str;
	str.clear();

	// Note: when the runtime thinks no-one's wearing the headset, it will go into SYNCHRONIZED mode, and stop taking controller inputs.
	// prevent this by blocking the light sensor on the inside of the headset with some tape etc.
	str+=fmt::format("XrSessionState: {0}\n",stringOf(xr_session_state));
	str+="Bound profile paths:\n";
	for(int i=0;i<activeInteractionProfilePaths.size();i++)
	{
		if(i)
			str+=", ";
		if(activeInteractionProfilePaths[i])
			str+=FromXrPath(xr_instance,activeInteractionProfilePaths[i]);
	}
	str+="\n";
	for(int i=0;i<ActionId::MAX_ACTIONS;i++)
	{
		auto &def=xr_input_session.actionDefinitions[i];
		auto &state=xr_input_session.actionStates[i];
		str+=def.name+": ";
		switch(def.xrActionType)
		{
			case XR_ACTION_TYPE_POSE_INPUT:
			{
				//teleport::core::Pose pose_stagespace=ConvertGLStageSpacePoseToLocalSpacePose(state.pose_stageSpace);
				//str+=fmt::format("{: .3f},{: .3f},{: .3f}    - {: .2f},{: .2f},{: .2f},{: .2f}",pose_stagespace.position.x,pose_stagespace.position.y,pose_stagespace.position.z
				//						,pose_stagespace.orientation.x,pose_stagespace.orientation.y,pose_stagespace.orientation.z,pose_stagespace.orientation.w);
			}
			break;
			case XR_ACTION_TYPE_BOOLEAN_INPUT:
			{
			//	str+=fmt::format("{0}",state.u32!=0?"true":"false");
			}
			break;
			case XR_ACTION_TYPE_FLOAT_INPUT:
			{
			//	str+=fmt::format("{: .3f}",state.f32);
			}
			break;
			case XR_ACTION_TYPE_VECTOR2F_INPUT:
			{
			//..	str+=fmt::format("{0},{1}",state.vec2f[0],state.vec2f[1]);
			}
			break;
			case XR_ACTION_TYPE_VIBRATION_OUTPUT:
			{
			}
			break;
			default:
			break;
		};
		str+="\n";
	}
	return str;
}