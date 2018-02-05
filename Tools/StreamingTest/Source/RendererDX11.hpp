// Copyright (c) 2018 Simul.co

#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include "Interfaces.hpp"

using Microsoft::WRL::ComPtr;

class RendererDX11 final : public RendererInterface
{
public:
	GLFWwindow* initialize(int width, int height) override;
	void render() override;

	Surface createSurface(SurfaceFormat format) override;
	void releaseSurface(Surface& surface) override;

	RendererDevice* getDevice() const override
	{ 
		return reinterpret_cast<RendererDevice*>(m_device.Get());
	}

private:
	void setupPipeline();

	ComPtr<ID3D11Buffer> createConstantBuffer(const void* data, UINT size) const;
	template<typename T> ComPtr<ID3D11Buffer> createConstantBuffer(const T* data=nullptr) const
	{
		return createConstantBuffer(data, sizeof(T));
	}

	struct FrameBuffer
	{
		UINT width, height;
		ComPtr<ID3D11Texture2D> texture;
		ComPtr<ID3D11ShaderResourceView> srv;
		ComPtr<ID3D11RenderTargetView> rtv;
	};
	FrameBuffer createFrameBuffer(UINT width, UINT height, DXGI_FORMAT format) const;

	ComPtr<ID3D11Device> m_device;
	ComPtr<ID3D11DeviceContext> m_context;
	ComPtr<IDXGISwapChain> m_swapChain;
	ComPtr<ID3D11RenderTargetView> m_backBufferRTV;

	ComPtr<ID3D11VertexShader> m_screenQuadVS;
	ComPtr<ID3D11PixelShader>  m_testRenderPS;
	ComPtr<ID3D11PixelShader>  m_displayPS;

	ComPtr<ID3D11RasterizerState> m_rasterizerState;
	ComPtr<ID3D11SamplerState> m_samplerState;

	ComPtr<ID3D11Buffer> m_renderCB;
	FrameBuffer m_renderFB;

	UINT m_frameWidth;
	UINT m_frameHeight;
};
