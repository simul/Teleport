// (C) Copyright 2018-2019 Simul Software Ltd
#pragma once

#include <api/Shader.h>

namespace pc_client
{
	//Interface for Shader
	class PC_Shader final : public scr::Shader
	{
	public:
		PC_Shader(scr::RenderPlatform *r):scr::Shader(r) {}

		void Create(const char* sourceCode, const char* entryPoint, Stage stage) override;
		void Compile() override;
	};
}