#include <stdexcept>

#include "RendererDX11.hpp"

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <dxgi.h>
#include <d3dcompiler.h>

// Compiled shaders
#include <ScreenQuad_DX11_VS.hpp>
#include <TestRender_DX11_PS.hpp>

struct RenderCB
{
	float iTime;
	float _padding[3];
};

GLFWwindow* RendererDX11::initialize(int width, int height)
{
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	GLFWwindow* window = glfwCreateWindow(width, height, "Direct3D 11 Renderer", nullptr, nullptr);
	if(!window) {
		throw std::runtime_error("Failed to create window");
	}

	UINT deviceFlags = 0;
#if _DEBUG
	deviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	const D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
	if(FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, deviceFlags, &featureLevel, 1, D3D11_SDK_VERSION, &m_device, nullptr, &m_context))) {
		throw std::runtime_error("Failed to create D3D11 device");
	}

	ComPtr<IDXGIFactory1> dxgiFactory;
	{
		ComPtr<IDXGIDevice> dxgiDevice;
		if(SUCCEEDED(m_device.As<IDXGIDevice>(&dxgiDevice))) {
			ComPtr<IDXGIAdapter> dxgiAdapter;
			if(SUCCEEDED(dxgiDevice->GetAdapter(&dxgiAdapter))) {
				dxgiAdapter->GetParent(IID_PPV_ARGS(&dxgiFactory));
			}
		}
	}
	if(!dxgiFactory) {
		throw std::runtime_error("Failed to retrieve the IDXGIFactory1 interface associated with D3D11 device");
	}

	DXGI_SWAP_CHAIN_DESC swapChainDesc ={};
	swapChainDesc.BufferCount = 1;
	swapChainDesc.BufferDesc.Width = width;
	swapChainDesc.BufferDesc.Height = height;
	swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	swapChainDesc.OutputWindow = glfwGetWin32Window(window);
	swapChainDesc.Windowed = true;

	if(FAILED(dxgiFactory->CreateSwapChain(m_device.Get(), &swapChainDesc, &m_swapChain))) {
		throw std::runtime_error("Failed to create the swap chain");
	}
	dxgiFactory->MakeWindowAssociation(glfwGetWin32Window(window), DXGI_MWA_NO_ALT_ENTER);

	{
		ComPtr<ID3D11Texture2D> backBuffer;
		m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
		if(FAILED(m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, &m_backBufferRTV))) {
			throw std::runtime_error("Failed to create window back buffer render target view");
		}
	}

	D3D11_VIEWPORT viewport ={};
	viewport.Width    = (FLOAT)width;
	viewport.Height   = (FLOAT)height;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	m_context->RSSetViewports(1, &viewport);

	setupPipeline();

	return window;
}
	
void RendererDX11::setupPipeline()
{
	if(FAILED(m_device->CreateVertexShader(ScreenQuad_DX11_VS, sizeof(ScreenQuad_DX11_VS), nullptr, &m_screenQuadVS))) {
		throw std::runtime_error("Failed to create screen quad vertex shader");
	}
	if(FAILED(m_device->CreatePixelShader(TestRender_DX11_PS, sizeof(TestRender_DX11_PS), nullptr, &m_testRenderPS))) {
		throw std::runtime_error("Failed to create test render pixel shader");
	}

	D3D11_RASTERIZER_DESC rasterizerDesc ={};
	rasterizerDesc.FillMode = D3D11_FILL_SOLID;
	rasterizerDesc.CullMode = D3D11_CULL_NONE;
	if(FAILED(m_device->CreateRasterizerState(&rasterizerDesc, &m_rasterizerState))) {
		throw std::runtime_error("Failed to create default rasterizer state");
	}

	m_renderCB = createConstantBuffer<RenderCB>();
}

void RendererDX11::render()
{
	{
		RenderCB renderConstants;
		renderConstants.iTime = static_cast<float>(glfwGetTime());
		m_context->UpdateSubresource(m_renderCB.Get(), 0, nullptr, &renderConstants, 0, 0);
	}

	m_context->OMSetRenderTargets(1, m_backBufferRTV.GetAddressOf(), nullptr);
	m_context->RSSetState(m_rasterizerState.Get());

	m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	m_context->IASetInputLayout(nullptr);
	m_context->PSSetConstantBuffers(0, 1, m_renderCB.GetAddressOf());
	m_context->VSSetShader(m_screenQuadVS.Get(), nullptr, 0);
	m_context->PSSetShader(m_testRenderPS.Get(), nullptr, 0);
	m_context->Draw(4, 0);

	m_swapChain->Present(1, 0);
}

ComPtr<ID3D11Buffer> RendererDX11::createConstantBuffer(const void* data, UINT size) const
{
	D3D11_BUFFER_DESC desc ={};
	desc.ByteWidth = static_cast<UINT>(size);
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

	D3D11_SUBRESOURCE_DATA bufferData ={};
	bufferData.pSysMem = data;

	ComPtr<ID3D11Buffer> buffer;
	const D3D11_SUBRESOURCE_DATA* bufferDataPtr = data ? &bufferData : nullptr;
	if(FAILED(m_device->CreateBuffer(&desc, bufferDataPtr, &buffer))) {
		throw std::runtime_error("Failed to create constant buffer");
	}
	return buffer;
}
