#include <cstdio>
#include <memory>

#include "RendererDX11.hpp"

#include <GLFW/glfw3.h>

int main(int argc, char* argv[])
{
	glfwInit();

	{
		glfwWindowHint(GLFW_RESIZABLE, 0);

		std::unique_ptr<RendererInterface> renderer(new RendererDX11);
		GLFWwindow* window = renderer->initialize(1024, 1024);

		glfwSetTime(0.0f);
		while(!glfwWindowShouldClose(window)) {
			renderer->render();
			glfwPollEvents();
		}
	}

	glfwTerminate();
	return 0;
}
