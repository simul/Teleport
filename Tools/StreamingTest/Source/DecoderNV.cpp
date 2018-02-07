// Copyright (c) 2018 Simul.co

#include <stdexcept>
#include <Windows.h>

#include "DecoderNV.hpp"

#define DEBUG_DUMP_YUV 1

#if DEBUG_DUMP_YUV
#include <fstream>
#include <memory>
#endif

#define CUFAILED(x) \
	((x) != CUDA_SUCCESS)

#define GETPROC(proc) \
	proc = (t##proc*)GetProcAddress(hLibrary, #proc)
#define GETPROC_EX(proc, name) \
	proc = (t##proc*)GetProcAddress(hLibrary, name)

static tcuCtxCreate* cuCtxCreate;
static tcuCtxDestroy* cuCtxDestroy;
static tcuD3D11GetDevices* cuD3D11GetDevices;
static tcuGraphicsD3D11RegisterResource* cuGraphicsD3D11RegisterResource;
static tcuGraphicsUnregisterResource* cuGraphicsUnregisterResource;
#if DEBUG_DUMP_YUV
static tcuMemAllocHost* cuMemAllocHost;
static tcuMemFreeHost* cuMemFreeHost;
static tcuMemcpyDtoH* cuMemcpyDtoH;
#endif

static tcuvidCreateVideoParser* cuvidCreateVideoParser;
static tcuvidParseVideoData* cuvidParseVideoData;
static tcuvidDestroyVideoParser* cuvidDestroyVideoParser;
static tcuvidCreateDecoder* cuvidCreateDecoder;
static tcuvidDestroyDecoder* cuvidDestroyDecoder;
static tcuvidDecodePicture* cuvidDecodePicture;
static tcuvidMapVideoFrame* cuvidMapVideoFrame;
static tcuvidUnmapVideoFrame* cuvidUnmapVideoFrame;

void DecoderNV::initializeCUDA()
{
	static HMODULE hLibrary = nullptr;
	if(hLibrary) {
		return;
	}

	hLibrary = LoadLibrary(L"nvcuda.dll");
	if(!hLibrary) {
		throw std::runtime_error("Failed to load nvcuda library");
	}

	tcuInit* cuInit = (tcuInit*)GetProcAddress(hLibrary, "cuInit");
	if(!cuInit || CUFAILED(cuInit(0))) {
		throw std::runtime_error("Failed initialize CUDA DRIVER API");
	}

	GETPROC(cuCtxCreate);
	GETPROC(cuCtxDestroy);
	GETPROC(cuD3D11GetDevices);
	GETPROC(cuGraphicsD3D11RegisterResource);
	GETPROC(cuGraphicsUnregisterResource);
#if DEBUG_DUMP_YUV
	GETPROC(cuMemAllocHost);
	GETPROC(cuMemFreeHost);
	GETPROC(cuMemcpyDtoH);
#endif
}

void DecoderNV::initializeCUVID()
{
	static HMODULE hLibrary = nullptr;
	if(hLibrary) {
		return;
	}

	hLibrary = LoadLibrary(L"nvcuvid.dll");
	if(!hLibrary) {
		throw std::runtime_error("Failed to load nvcuvid library");
	}

	GETPROC(cuvidCreateVideoParser);
	GETPROC(cuvidDestroyVideoParser);
	GETPROC(cuvidParseVideoData);
	GETPROC(cuvidCreateDecoder);
	GETPROC(cuvidDestroyDecoder);
	GETPROC(cuvidDecodePicture);
#ifdef _WIN64
	GETPROC_EX(cuvidMapVideoFrame, "cuvidMapVideoFrame64");
	GETPROC_EX(cuvidUnmapVideoFrame, "cuvidUnmapVideoFrame64");
#else
	GETPROC_EX(cuvidMapVideoFrame, "cuvidMapVideoFrame");
	GETPROC_EX(cuvidUnmapVideoFrame, "cuvidUnmapVideoFrame");
#endif
}

DecoderNV::DecoderNV()
{
	initializeCUDA();
	initializeCUVID();
}

void DecoderNV::initialize(RendererDevice* device, int width, int height)
{
	m_frameWidth = width;
	m_frameHeight = height;

	{
		unsigned int numDevices;
		if(CUFAILED(cuD3D11GetDevices(&numDevices, &m_device, 1, reinterpret_cast<ID3D11Device*>(device), CU_D3D11_DEVICE_LIST_ALL))) {
			throw std::runtime_error("D3D11 device is not CUDA capable");
		}
		if(CUFAILED(cuCtxCreate(&m_context, 0, m_device))) {
			throw std::runtime_error("Failed to create CUDA context");
		}
	}

	{
		CUVIDPARSERPARAMS params = {};
		params.CodecType = cudaVideoCodec_H264;
		params.ulMaxNumDecodeSurfaces = 1;
		params.pUserData = this;
		params.pfnSequenceCallback = DecoderNV::onSequence;
		params.pfnDecodePicture = DecoderNV::onDecode;
		params.pfnDisplayPicture = DecoderNV::onDisplay;
		if(CUFAILED(cuvidCreateVideoParser(&m_parser, &params))) {
			throw std::runtime_error("Failed to create video stream parser");
		}
	}

	{
		CUVIDDECODECREATEINFO params = {};
		params.ulWidth = (unsigned long)width;
		params.ulHeight = (unsigned long)height;
		params.ulTargetWidth = params.ulWidth;
		params.ulTargetHeight = params.ulHeight;
		params.CodecType = cudaVideoCodec_H264;
		params.ChromaFormat = cudaVideoChromaFormat_420;
		params.OutputFormat = cudaVideoSurfaceFormat_NV12;
		params.DeinterlaceMode = cudaVideoDeinterlaceMode_Weave;
		params.ulNumDecodeSurfaces = 1;
		params.ulNumOutputSurfaces = 1;
		params.display_area = {0, 0, (short)width, (short)height};
		if(CUFAILED(cuvidCreateDecoder(&m_decoder, &params))) {
			throw std::runtime_error("Failed to create video decoder");
		}
	}
}

void DecoderNV::shutdown()
{
	cuvidDestroyDecoder(m_decoder);
	cuvidDestroyVideoParser(m_parser);
	cuGraphicsUnregisterResource(m_registeredSurface);
	cuCtxDestroy(m_context);
}
	
void DecoderNV::decode(Bitstream& stream)
{
	CUVIDSOURCEDATAPACKET packet = {};
	packet.payload = reinterpret_cast<unsigned char*>(stream.pData);
	packet.payload_size = (unsigned long)stream.numBytes;

	if(CUFAILED(cuvidParseVideoData(m_parser, &packet))) {
		throw std::runtime_error("Failed to parse video stream");
	}
	stream.free();
}
	
void DecoderNV::registerSurface(const Surface& surface)
{
	if(CUFAILED(cuGraphicsD3D11RegisterResource(&m_registeredSurface, reinterpret_cast<ID3D11Resource*>(surface.pResource), 0))) {
		throw std::runtime_error("Failed to register D3D11 surface with CUDA");
	}
}
	
int DecoderNV::onSequence(void* pThis, CUVIDEOFORMAT* format)
{
	//printf("SEQ\n");
	return 0;
}
	
int DecoderNV::onDecode(void* pThis, CUVIDPICPARAMS* pic)
{
	DecoderNV* _this = reinterpret_cast<DecoderNV*>(pThis);
	if(CUFAILED(cuvidDecodePicture(_this->m_decoder, pic))) {
		throw std::runtime_error("Failed to decode picture frame");
	}

#if DEBUG_DUMP_YUV
	CUdeviceptr frameDevicePtr;
	unsigned int framePitch;

	CUVIDPROCPARAMS procParams = {};
	procParams.progressive_frame = 1;
	if(cuvidMapVideoFrame(_this->m_decoder, pic->CurrPicIdx, &frameDevicePtr, &framePitch, &procParams) == CUDA_SUCCESS) {
		const size_t frameNumBytes = _this->m_frameHeight * framePitch * 3 / 2;
		void* frameHostPtr;

		if(CUFAILED(cuMemAllocHost(&frameHostPtr, frameNumBytes))) {
			throw std::runtime_error("Failed to allocate host memory for dumping frame data");
		}
		cuMemcpyDtoH(frameHostPtr, frameDevicePtr, frameNumBytes);
		cuvidUnmapVideoFrame(_this->m_decoder, frameDevicePtr);
		{
			std::ofstream dumpStream{"debug.yuv", std::ios::binary | std::ios::app};
			dumpStream.write(reinterpret_cast<const char*>(frameHostPtr), frameNumBytes);
		}
		cuMemFreeHost(frameHostPtr);
		std::fputc('.', stdout);
	}
	else {
		throw std::runtime_error("Failed to map video frame memory");
	}

#endif
	return 0;
}
	
int DecoderNV::onDisplay(void* pThis, CUVIDPARSERDISPINFO* dispInfo)
{
	//printf("DISPLAY\n");
	return 0;
}
