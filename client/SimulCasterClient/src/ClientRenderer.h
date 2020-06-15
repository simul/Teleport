//
// (C) Copyright 2018 Simul
#pragma once
#include "App.h"
#include "basic_linear_algebra.h"
#include "ResourceCreator.h"
#include "crossplatform/SessionClient.h"
#include "GlobalGraphicsResources.h"
#include <SurfaceTexture.h>
#include <GuiSys.h>
#include <libavstream/decoder.hpp>
#include <libavstream/libavstream.hpp>
#include <OVR_Input.h>

class ClientAppInterface
{
public:
	virtual std::string LoadTextFile(const char *filename)=0;
	virtual const scr::Effect::EffectPassCreateInfo& BuildEffectPass(const char* effectPassName, scr::VertexBufferLayout* vbl, const scr::ShaderSystem::PipelineCreateInfo*, const std::vector<scr::ShaderResource>& shaderResources)=0;

};

class ClientRenderer
{
public:
	ClientRenderer(ResourceCreator *r,scr::ResourceManagers *rm,SessionCommandInterface *i,ClientAppInterface *c);
	~ClientRenderer();

	void ToggleTextures();
	void ToggleShowInfo();
	void  SetStickOffset(float,float);

	void EnteredVR(struct ovrMobile *ovrMobile,const ovrJava *java);
	void ExitedVR();
	void Render(const OVR::ovrFrameInput& vrFrame,OVR::OvrGuiSys *mGuiSys);
	void CopyToCubemaps(scc::GL_DeviceContext &mDeviceContext);
	void UpdateHandObjects();
	void RenderLocalActors(OVR::ovrFrameResult& res);

	avs::Decoder       mDecoder;
	avs::NetworkSource mNetworkSource;

	avs::vec3 oculusOrigin;		// in metres. The headPose will be relative to this.

	scr::ResourceManagers	*resourceManagers	=nullptr;
	ResourceCreator			*resourceCreator	=nullptr;
	ClientAppInterface		*clientAppInterface	=nullptr;
	ovrMobile				*mOvrMobile			=nullptr;
	avs::HeadPose headPose;
	avs::HeadPose controllerPoses[2];
	avs::vec3 cameraPosition;	// in real space.
	const scr::quat HAND_ROTATION_DIFFERENCE {0.0000000456194194, 0.923879385, -0.382683367, 0.000000110135019}; //Adjustment to the controller's rotation to get the desired rotation.

	struct VideoUB
	{
		avs::vec4 eyeOffsets[2];
		ovrMatrix4f invViewProj[2];
		avs::vec3 cameraPosition;
		int pad_;
	};
	VideoUB videoUB;

	struct CubemapUB
	{
		scr::ivec2 sourceOffset;
		uint32_t   faceSize;
		uint32_t    mip = 0;
		uint32_t    face = 0;
	};
	CubemapUB cubemapUB;

	OVR::ovrSurfaceDef mVideoSurfaceDef;
	OVR::GlProgram     mVideoSurfaceProgram;
	OVR::SurfaceTexture *mVideoSurfaceTexture=nullptr;
	std::shared_ptr<scr::Texture>       mVideoTexture;
	std::shared_ptr<scr::Texture>       mCubemapTexture;
	std::shared_ptr<scr::Texture>       mDiffuseTexture;
	std::shared_ptr<scr::Texture>       mSpecularTexture;
	std::shared_ptr<scr::Texture>       mRoughSpecularTexture;
	std::shared_ptr<scr::Texture>       mCubemapLightingTexture;
	std::shared_ptr<scr::UniformBuffer> mCubemapUB;
	std::shared_ptr<scr::UniformBuffer> mVideoUB;
	std::shared_ptr<scr::ShaderStorageBuffer> mCameraPositionBuffer;
	std::vector<scr::ShaderResource>    mCubemapComputeShaderResources;
	std::shared_ptr<scr::Effect>        mCopyCubemapEffect;
	std::shared_ptr<scr::Effect>        mCopyCubemapWithDepthEffect;
	std::shared_ptr<scr::Effect>        mExtractCameraPositionEffect;

	GlobalGraphicsResources& GlobalGraphicsResources = GlobalGraphicsResources::GetInstance();

	avs::vec4 mCameraPositions[8];

	std::string                         CopyCubemapSrc;
	std::string                         ExtractPositionSrc;
	int specularSize = 128;
	int diffuseSize = 64;
	int lightSize = 64;
	bool mShowInfo=true;
};