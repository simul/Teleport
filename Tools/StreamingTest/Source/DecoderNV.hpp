// Copyright (c) 2018 Simul.co

#pragma once

#include "Interfaces.hpp"

#define INIT_CUDA_D3D11 1
#include <dynlink_cuda.h>
#include <dynlink_cudaD3D11.h>
#include <dynlink_nvcuvid.h>

class DecoderNV final : public DecoderInterface
{
public:
	DecoderNV();

	void initialize(RendererDevice* device, int width, int height) override;
	void shutdown() override;
	void decode(Bitstream& stream) override;
	
	void registerSurface(const Surface& surface) override;

private:
	static void initializeCUDA();
	static void initializeCUVID();

	static int onSequence(void* pThis, CUVIDEOFORMAT* format);
	static int onDecode(void* pThis, CUVIDPICPARAMS* pic);
	static int onDisplay(void* pThis, CUVIDPARSERDISPINFO* dispInfo);

	CUdevice m_device;
	CUcontext m_context;
	CUvideoparser m_parser;
	CUvideodecoder m_decoder;

	int m_frameWidth;
	int m_frameHeight;
	CUgraphicsResource m_registeredSurface;
};
