#include <cstdio>
#include <memory>

#include "RendererDX11.hpp"
#include "EncoderNV.hpp"

#include <GLFW/glfw3.h>

int main(int argc, char* argv[])
{
	glfwInit();

	{
		glfwWindowHint(GLFW_RESIZABLE, 0);

		std::unique_ptr<RendererInterface> renderer(new RendererDX11);
		std::unique_ptr<EncoderInterface> encoder(new EncoderNV);

		GLFWwindow* window = renderer->initialize(1024, 1024);
		encoder->initialize(renderer->getDevice());

		glfwSetTime(0.0f);
		while(!glfwWindowShouldClose(window)) {
			renderer->render();
			glfwPollEvents();
		}
	}

	glfwTerminate();
	return 0;
}
