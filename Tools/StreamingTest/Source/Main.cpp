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

int main(int argc, char* argv[])
{
	glfwInit();
	glfwWindowHint(GLFW_RESIZABLE, 0);

#if ENCODE
	{
		std::unique_ptr<RendererInterface> renderer(new RendererDX11);
		std::unique_ptr<EncoderInterface> encoder(new EncoderNV);

		FileWriter writer{"output.h264"};

		GLFWwindow* window = renderer->initialize(g_frameWidth, g_frameHeight);
		encoder->initialize(renderer->getDevice(), g_frameWidth, g_frameHeight);

		Surface surface = renderer->createSurface(encoder->getInputFormat());
		encoder->registerSurface(surface);

		uint64_t frameIndex = 0;
		glfwSetTime(0.0);
		while(!glfwWindowShouldClose(window)) {
			renderer->render();
			encoder->encode(frameIndex++);

			const Bitstream bitstream = encoder->lock();
			writer.write(bitstream);
			encoder->unlock();

			glfwPollEvents();
		}

		encoder->shutdown();
		renderer->releaseSurface(surface);
	}
#else
	{
		std::unique_ptr<RendererInterface> renderer(new RendererDX11);
		std::unique_ptr<DecoderInterface> decoder(new DecoderNV);

		FileReader reader{"output.h264", 1024*1024};

		GLFWwindow* window = renderer->initialize(g_frameWidth, g_frameHeight);
		Surface surface = renderer->createSurface(SurfaceFormat::ARGB);

		decoder->initialize(renderer->getDevice(), g_frameWidth, g_frameHeight);
		decoder->registerSurface(surface);

		do {
			Bitstream stream = reader.read();
			decoder->decode(stream);
		} while(reader);
	}
#endif

	glfwTerminate();
	return 0;
}
