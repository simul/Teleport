// Copyright (c) 2018 Simul.co

#include <cstdio>
#include <memory>

#include "RendererDX11.hpp"
#include "EncoderNV.hpp"
#include "DecoderNV.hpp"
#include "FileIO.hpp"

#include <GLFW/glfw3.h>
	
const int g_frameWidth  = 1024;
const int g_frameHeight = 1024;

#define ENCODE 0

int main(int argc, char* argv[])
{
	glfwInit();
	glfwWindowHint(GLFW_RESIZABLE, 0);

#if ENCODE
	{
		std::shared_ptr<RendererInterface> renderer(new RendererDX11);
		std::unique_ptr<EncoderInterface> encoder(new EncoderNV);

		FileWriter writer{"output.h264"};

		GLFWwindow* window = renderer->initialize(g_frameWidth, g_frameHeight);
		encoder->initialize(renderer, g_frameWidth, g_frameHeight);

		uint64_t frameIndex = 0;
		glfwSetTime(0.0);
		while(!glfwWindowShouldClose(window)) {
			renderer->renderScene();
			encoder->encode(frameIndex++);
			renderer->renderSurface();

			const Bitstream bitstream = encoder->lock();
			writer.write(bitstream);
			encoder->unlock();

			glfwPollEvents();
		}

		encoder->shutdown();
	}
#else
	{
		std::shared_ptr<RendererInterface> renderer(new RendererDX11);
		std::unique_ptr<DecoderInterface> decoder(new DecoderNV);

		FileReader reader{"output.h264", 64*1024};

		GLFWwindow* window = renderer->initialize(g_frameWidth, g_frameHeight);
		Surface surface = renderer->createSurface(SurfaceFormat::ARGB);

		decoder->initialize(renderer, g_frameWidth, g_frameHeight);

		do {
			Bitstream stream = reader.read();
			decoder->decode(stream);

			renderer->renderVideo();
			renderer->renderSurface();

			glfwPollEvents();
		} while(reader);
	}
#endif

	glfwTerminate();
	return 0;
}
