// Copyright (c) 2018 Simul.co

#include <cstdio>
#include <memory>

#include "RendererDX11.hpp"
#include "EncoderNV.hpp"
#include "FileIO.hpp"

#include <GLFW/glfw3.h>

int main(int argc, char* argv[])
{
	glfwInit();

	{
		glfwWindowHint(GLFW_RESIZABLE, 0);

		std::unique_ptr<RendererInterface> renderer(new RendererDX11);
		std::unique_ptr<EncoderInterface> encoder(new EncoderNV);

		FileWriter writer{"output.raw"};

		const int frameWidth  = 1024;
		const int frameHeight = 1024;
		GLFWwindow* window = renderer->initialize(frameWidth, frameHeight);
		encoder->initialize(renderer->getDevice(), frameWidth, frameHeight);

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

	glfwTerminate();
	return 0;
}
