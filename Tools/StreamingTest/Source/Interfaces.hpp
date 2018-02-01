#pragma once

typedef struct GLFWwindow GLFWwindow;

class RendererInterface
{
public:
	virtual GLFWwindow* initialize(int width, int height) = 0;
	virtual void render() = 0;
};
