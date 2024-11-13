// (C) Copyright 2018-2022 Simul Software Ltd
#pragma once

//C Libraries
#include <iostream>
#include <string>
#include <assert.h>

//STL
#include <vector>
#include <deque>
#include <map>
#include <memory>
#include <algorithm>
#include <functional>
#include <libavstream/geometry/mesh_interface.hpp>
#include <random>

// Platform
#include "Platform/CrossPlatform/RenderPlatform.h"



#if defined(__ANDROID__)
#include <android/log.h>
#define FILE_LINE (std::string(__FILE__) + std::string("(") +  std::to_string(__LINE__) + std::string("):")).c_str()
#define SCR_LOG(...) __android_log_print(ANDROID_LOG_INFO, "SCR", __VA_ARGS__);
#else
extern void log_print(const char* source,const char *format, ...);
#define SCR_LOG(fmt,...) log_print( "SCR", fmt, __VA_ARGS__);
#endif

namespace teleport
{
	namespace clientrender
	{
		class RenderPlatform;
		class APIObject
		{
		protected:
			 platform::crossplatform::RenderPlatform * renderPlatform;
			APIObject( platform::crossplatform::RenderPlatform* r) 
				: renderPlatform(r) {}
		};

		enum class BufferUsageBit : uint32_t
		{
			UNKNOWN_BIT = 0x00000000,
			STREAM_BIT	= 0x00000001,
			STATIC_BIT	= 0x00000002,
			DYNAMIC_BIT = 0x00000004,
			READ_BIT	= 0x00000008,
			WRITE_BIT	= 0x00000010,
			COPY_BIT	= 0x00000020,
			DRAW_BIT	= 0x00000040
		};
	
		inline BufferUsageBit operator|(BufferUsageBit a, BufferUsageBit b)
		{
			return static_cast<BufferUsageBit>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
		}

		// Taken from Unity
		enum class LightType : char
		{
			//
			// Summary:
			//     The light is a spot light.
			Spot = 0,
			//
			// Summary:
			//     The light is a directional light.
			Directional = 1,
			//
			// Summary:
			//     The light is a point light.
			Point = 2,
			Area = 3,
			//
			// Summary:
			//     The light is a rectangle shaped area light. It affects only baked lightmaps and
			//     lightprobes.
			Rectangle = 3,
			//
			// Summary:
			//     The light is a disc shaped area light. It affects only baked lightmaps and lightprobes.
			Disc = 4
		};

		struct SceneCaptureCubeCoreTagData
		{
	#pragma pack(push, 1)
			uint64_t timestamp_unix_ms;

			uint32_t id;
			avs::Transform cameraTransform;
			uint32_t lightCount;
			float diffuseAmbientScale;
	#pragma pack(pop)
		}
	#ifdef __ANDROID__
		__attribute__((packed))
	#endif
		;

		struct LightTagData
		{
	#pragma pack(push, 1)
			avs::Transform worldTransform;
			vec4 color;
			float range;
			float spotAngle;
			LightType lightType;
			vec3 position;
			vec4 orientation;
			float shadowProjectionMatrix[4][4];
			float worldToShadowMatrix[4][4];
			int texturePosition[2];
			int textureSize;
			uint64_t uid;
	#pragma pack(pop)
		}
	#ifdef __ANDROID__
		__attribute__((packed))
	#endif
		;

		struct SceneCaptureCubeTagData
		{
			SceneCaptureCubeCoreTagData coreData;
			std::vector<LightTagData> lights;
		};
	}
}