// (C) Copyright 2018-2023 Simul Software Ltd
#pragma once
 
#include "Common.h"
#include "TeleportCore/FontAtlas.h"
#include "ResourceManager.h"
namespace platform
{
	namespace crossplatform
	{
		class Texture;
	}
}

namespace clientrender
{
	class Texture;
	//! A font atlas.
	class FontAtlas:public teleport::core::FontAtlas, public IncompleteFontAtlas
	{
	public:
		FontAtlas(avs::uid u=0):teleport::core::FontAtlas(u),IncompleteFontAtlas(u){}
		std::shared_ptr<clientrender::Texture> fontTexture;
		std::string getName() const
		{
			return "FontAtlas";
		}
	};
}