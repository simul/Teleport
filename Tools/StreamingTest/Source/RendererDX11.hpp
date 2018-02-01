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

private:
	void setupPipeline();

	ComPtr<ID3D11Buffer> createConstantBuffer(const void* data, UINT size) const;
	template<typename T> ComPtr<ID3D11Buffer> createConstantBuffer(const T* data=nullptr) const
	{
		return createConstantBuffer(data, sizeof(T));
	}

	ComPtr<ID3D11Device> m_device;
	ComPtr<ID3D11DeviceContext> m_context;
	ComPtr<IDXGISwapChain> m_swapChain;
	ComPtr<ID3D11RenderTargetView> m_backBufferRTV;

	ComPtr<ID3D11VertexShader> m_screenQuadVS;
	ComPtr<ID3D11PixelShader>  m_testRenderPS;
	ComPtr<ID3D11RasterizerState> m_rasterizerState;

	ComPtr<ID3D11Buffer> m_renderCB;
};
