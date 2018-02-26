// Copyright (c) 2018 Simul.co

#include <cstdio>
#include <memory>

#include "RendererDX11.hpp"
#include "EncoderNV.hpp"
#include "DecoderNV.hpp"
#include "FileIO.hpp"
#include "NetworkStream.hpp"

#include <GLFW/glfw3.h>

using namespace Streaming;
	
const int g_frameWidth   = 2048;
const int g_frameHeight  = 1024;
const int g_idrFrequency = 60;
const int g_port         = 31337;

static void serverMain()
{
	std::unique_ptr<RendererInterface> renderer(new RendererDX11);
	std::unique_ptr<EncoderInterface> encoder(new EncoderNV);

	NetworkStream stream;
	stream.listen(g_port);

	GLFWwindow* window = renderer->initialize("Streaming Server", g_frameWidth, g_frameHeight);
	encoder->initialize(renderer.get(), g_frameWidth, g_frameHeight, g_idrFrequency);

	uint64_t frameIndex = 0;
	glfwSetTime(0.0);
	while(!glfwWindowShouldClose(window)) {
		renderer->renderScene();
		encoder->encode(frameIndex++);
		renderer->renderSurface();

		stream.processServer();

		const Bitstream bitstream = encoder->lock();
		stream.write(bitstream);
		encoder->unlock();

		glfwPollEvents();
	}

	encoder->shutdown();
}

static void clientMain(const char* hostName)
{
	std::unique_ptr<RendererInterface> renderer(new RendererDX11);
	std::unique_ptr<DecoderInterface> decoder(new DecoderNV);

	NetworkStream stream;
	stream.connect(hostName, g_port);

	GLFWwindow* window = renderer->initialize("Streaming Client", g_frameWidth, g_frameHeight);
	Surface surface = renderer->createSurface(SurfaceFormat::ARGB);

	decoder->initialize(renderer.get(), g_frameWidth, g_frameHeight);
	
	while(!glfwWindowShouldClose(window) && stream.processClient()) {
		Bitstream bitstream = stream.read();
		if(bitstream) {
			decoder->decode(bitstream);
		}

		renderer->renderVideo();
		renderer->renderSurface();

		glfwPollEvents();
	}
}

int main(int argc, char* argv[])
{
	glfwInit();
	glfwWindowHint(GLFW_RESIZABLE, 0);

	try {
		if(argc > 1 && std::strcmp(argv[1], "-server") == 0) {
			serverMain();
		}
		else {
			clientMain(argc > 1 ? argv[1] : "localhost");
		}
	}
	catch(const std::exception& e) {
		std::fprintf(stderr, "Error: %s\n", e.what());
		return 1;
	}

	glfwTerminate();
	return 0;
}
