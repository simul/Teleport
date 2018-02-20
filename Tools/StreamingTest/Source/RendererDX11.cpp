// Copyright (c) 2018 Simul.co

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
#include <Display_DX11_PS.hpp>
#include <NV12toRGBA_CS.hpp>

using namespace Streaming;

static ID3D11ShaderResourceView* const nullSRV[] = { nullptr };
static ID3D11UnorderedAccessView* const nullUAV[] = { nullptr };

struct RenderCB
{
	float iTime;
	float _padding[3];
};
struct VideoCB
{
	unsigned int pitch;
	float _padding[3];
};

GLFWwindow* RendererDX11::initialize(const char* title, int width, int height)
{
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	GLFWwindow* window = glfwCreateWindow(width, height, title, nullptr, nullptr);
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

	m_frameWidth = width;
	m_frameHeight = height;

	setupPipeline();

	return window;
}
	
Surface RendererDX11::createSurface(SurfaceFormat format)
{
	DXGI_FORMAT dxgiFormat = DXGI_FORMAT_UNKNOWN;
	switch(format) {
	case SurfaceFormat::ABGR: dxgiFormat = DXGI_FORMAT_B8G8R8A8_UNORM; break;
	case SurfaceFormat::ARGB: dxgiFormat = DXGI_FORMAT_R8G8B8A8_UNORM; break;
	case SurfaceFormat::NV12: dxgiFormat = DXGI_FORMAT_NV12; break;
	default:
		throw std::invalid_argument("Invalid buffer format");
	}

	m_renderFB = createFrameBuffer(m_frameWidth, m_frameHeight, dxgiFormat);

	ID3D11Resource* pResource = m_renderFB.texture.Get();
	pResource->AddRef();
	return {(int)m_renderFB.width, (int)m_renderFB.height, reinterpret_cast<void*>(pResource)};
}
	
void RendererDX11::releaseSurface(Surface& surface)
{
	ID3D11Resource* pResource = reinterpret_cast<ID3D11Resource*>(surface.pResource);
	pResource->Release();
	surface = {};
}
	
Buffer RendererDX11::createVideoBuffer(SurfaceFormat format, int pitch)
{
	UINT numBytes;
	UINT stride;
	DXGI_FORMAT dxgiFormat;

	switch(format) {
	case SurfaceFormat::ABGR:
		numBytes = m_frameHeight * pitch * 4;
		stride = 4;
		dxgiFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
		break;
	case SurfaceFormat::ARGB:
		numBytes = m_frameHeight * pitch * 4;
		stride = 4;
		dxgiFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
		break;
	case SurfaceFormat::NV12:
		numBytes = m_frameHeight * pitch * 3 / 2;
		stride = 1;
		dxgiFormat = DXGI_FORMAT_R8_UINT;
		break;
	}

	m_videoPB = createPixelBuffer(numBytes, stride, dxgiFormat);
	m_videoPB.pitch = pitch;

	ID3D11Resource* pResource = m_videoPB.buffer.Get();
	pResource->AddRef();
	return Buffer{(int)numBytes, reinterpret_cast<void*>(pResource)};
}

void RendererDX11::releaseVideoBuffer(Buffer& buffer)
{
	ID3D11Resource* pResource = reinterpret_cast<ID3D11Resource*>(buffer.pResource);
	pResource->Release();
	buffer = {};
}

void RendererDX11::setupPipeline()
{
	if(FAILED(m_device->CreateVertexShader(ScreenQuad_DX11_VS, sizeof(ScreenQuad_DX11_VS), nullptr, &m_screenQuadVS))) {
		throw std::runtime_error("Failed to create screen quad vertex shader");
	}
	if(FAILED(m_device->CreatePixelShader(TestRender_DX11_PS, sizeof(TestRender_DX11_PS), nullptr, &m_testRenderPS))) {
		throw std::runtime_error("Failed to create test render pixel shader");
	}
	if(FAILED(m_device->CreatePixelShader(Display_DX11_PS, sizeof(Display_DX11_PS), nullptr, &m_displayPS))) {
		throw std::runtime_error("Failed to create display pixel shader");
	}
	if(FAILED(m_device->CreateComputeShader(NV12toRGBA_CS, sizeof(NV12toRGBA_CS), nullptr, &m_nv12ToRgbCS))) {
		throw std::runtime_error("Failed to create NV12toRGBA compute shader");
	}

	D3D11_RASTERIZER_DESC rasterizerDesc = {};
	rasterizerDesc.FillMode = D3D11_FILL_SOLID;
	rasterizerDesc.CullMode = D3D11_CULL_NONE;
	if(FAILED(m_device->CreateRasterizerState(&rasterizerDesc, &m_rasterizerState))) {
		throw std::runtime_error("Failed to create default rasterizer state");
	}

	D3D11_SAMPLER_DESC samplerDesc = {};
	samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.MaxAnisotropy = 1;
	if(FAILED(m_device->CreateSamplerState(&samplerDesc, &m_samplerState))) {
		throw std::runtime_error("Failed to create sampler state");
	}
	
	m_renderCB = createConstantBuffer<RenderCB>();
	m_videoCB = createConstantBuffer<VideoCB>();
}

void RendererDX11::renderScene()
{
	{
		RenderCB renderConstants;
		renderConstants.iTime = static_cast<float>(glfwGetTime());
		m_context->UpdateSubresource(m_renderCB.Get(), 0, nullptr, &renderConstants, 0, 0);
	}
	
	m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	m_context->IASetInputLayout(nullptr);
	m_context->RSSetState(m_rasterizerState.Get());

	m_context->OMSetRenderTargets(1, m_renderFB.rtv.GetAddressOf(), nullptr);
	m_context->VSSetShader(m_screenQuadVS.Get(), nullptr, 0);
	m_context->PSSetShader(m_testRenderPS.Get(), nullptr, 0);
	m_context->PSSetConstantBuffers(0, 1, m_renderCB.GetAddressOf());
	m_context->Draw(4, 0);
}

void RendererDX11::renderVideo()
{
	{
		VideoCB videoConstants;
		videoConstants.pitch = m_videoPB.pitch;
		m_context->UpdateSubresource(m_videoCB.Get(), 0, nullptr, &videoConstants, 0, 0);
	}

	m_context->CSSetShader(m_nv12ToRgbCS.Get(), nullptr, 0);
	m_context->CSSetConstantBuffers(0, 1, m_videoCB.GetAddressOf());
	m_context->CSSetShaderResources(0, 1, m_videoPB.srv.GetAddressOf());
	m_context->CSSetUnorderedAccessViews(0, 1, m_renderFB.uav.GetAddressOf(), nullptr);
	m_context->Dispatch(m_frameWidth/32, m_frameHeight/32, 1);
	m_context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
}

void RendererDX11::renderSurface()
{
	m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	m_context->IASetInputLayout(nullptr);
	m_context->RSSetState(m_rasterizerState.Get());

	m_context->OMSetRenderTargets(1, m_backBufferRTV.GetAddressOf(), nullptr);
	m_context->VSSetShader(m_screenQuadVS.Get(), nullptr, 0);
	m_context->PSSetShader(m_displayPS.Get(), nullptr, 0);
	m_context->PSSetShaderResources(0, 1, m_renderFB.srv.GetAddressOf());
	m_context->PSSetSamplers(0, 1, m_samplerState.GetAddressOf());
	m_context->Draw(4, 0);
	m_context->PSSetShaderResources(0, 1, nullSRV);

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

RendererDX11::PixelBuffer RendererDX11::createPixelBuffer(UINT size, UINT stride, DXGI_FORMAT format) const
{
	PixelBuffer buf = {};

	D3D11_BUFFER_DESC desc = {};
	desc.ByteWidth = size;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	if(FAILED(m_device->CreateBuffer(&desc, nullptr, &buf.buffer))) {
		throw std::runtime_error("Failed to create structured buffer");
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.ElementOffset = 0;
	srvDesc.Buffer.ElementWidth  = size / stride;
	if(FAILED(m_device->CreateShaderResourceView(buf.buffer.Get(), &srvDesc, &buf.srv))) {
		throw std::runtime_error("Failed to create structured buffer view");
	}

	return buf;
}

RendererDX11::FrameBuffer RendererDX11::createFrameBuffer(UINT width, UINT height, DXGI_FORMAT format) const
{
	FrameBuffer fb;
	fb.width   = width;
	fb.height  = height;

	D3D11_TEXTURE2D_DESC desc ={};
	desc.Width = width;
	desc.Height = height;
	desc.Format = format;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.SampleDesc.Count = 1;
	desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

	if(FAILED(m_device->CreateTexture2D(&desc, nullptr, &fb.texture))) {
		throw std::runtime_error("Failed to create FrameBuffer backing texture");
	}

	D3D11_RENDER_TARGET_VIEW_DESC rtvDesc ={};
	rtvDesc.Format = desc.Format;
	rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	if(FAILED(m_device->CreateRenderTargetView(fb.texture.Get(), &rtvDesc, &fb.rtv))) {
		throw std::runtime_error("Failed to create FrameBuffer render target view");
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc ={};
	srvDesc.Format = desc.Format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;
	if(FAILED(m_device->CreateShaderResourceView(fb.texture.Get(), &srvDesc, &fb.srv))) {
		throw std::runtime_error("Failed to create FrameBuffer shader resource view");
	}

	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.Format = desc.Format;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
	uavDesc.Texture2D.MipSlice = 0;
	if(FAILED(m_device->CreateUnorderedAccessView(fb.texture.Get(), &uavDesc, &fb.uav))) {
		throw std::runtime_error("Failed to create FrameBuffer unordered access view");
	}
	return fb;
}
