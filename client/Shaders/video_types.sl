//  Copyright (c) 2020 Simul Software Ltd. All rights reserved.
#ifndef VIDEO_TYPES_SL
#define VIDEO_TYPES_SL

// Placeholders for lights
struct DirectionalLight
{
	vec3 direction;
	float pad;
	vec4 color;
	mat4 shadowViewMatrix;
	mat4 shadowProjectionMatrix;
};

struct PointLight
{
	vec3 position;
	float range;
	vec3 attenuation;
	float pad;
	vec4 color;
	mat4 shadowViewMatrix;
	mat4 shadowProjectionMatrix;
};

struct SpotLight
{
	vec3 position;
	float  range;
	vec3 direction;
	float cone;
	vec3 attenuation;
	float pad;
	vec4 color;
	mat4 shadowViewMatrix;
	mat4 shadowProjectionMatrix;
};

struct LightTag
{
	mat4 worldToShadowMatrix;
	vec2 shadowTexCoordOffset;
	vec2 shadowTexCoordScale;
	vec4 colour;
	vec3 position;
	float range;
	vec3 direction;
	uint uid32;
	float is_spot;
	float is_point;
	float shadow_strength;
	float radius;
};

struct VideoTagDataCube
{
	vec3 cameraPosition;
	int lightCount;
	vec4 cameraRotation;
	float diffuseAmbientScale;
	// Some light information 
	LightTag lightTags[10];
};

#endif
