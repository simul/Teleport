// Copyright (c) 2018 Simul.co

#include <stdexcept>
#include <Windows.h>

#include "DecoderNV.hpp"

#define DEBUG_DUMP_YUV 0

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
static tcuMemcpyDtoD* cuMemcpyDtoD;
static tcuD3D11GetDevices* cuD3D11GetDevices;
static tcuGraphicsD3D11RegisterResource* cuGraphicsD3D11RegisterResource;
static tcuGraphicsUnregisterResource* cuGraphicsUnregisterResource;
static tcuGraphicsMapResources* cuGraphicsMapResources; 
static tcuGraphicsUnmapResources* cuGraphicsUnmapResources;
static tcuGraphicsResourceSetMapFlags* cuGraphicsResourceSetMapFlags;
static tcuGraphicsResourceGetMappedPointer* cuGraphicsResourceGetMappedPointer;
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

using namespace Streaming;

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
	GETPROC(cuMemcpyDtoD);
	GETPROC(cuD3D11GetDevices);
	GETPROC(cuGraphicsD3D11RegisterResource);
	GETPROC(cuGraphicsUnregisterResource);
	GETPROC(cuGraphicsMapResources);
	GETPROC(cuGraphicsUnmapResources);
	GETPROC(cuGraphicsResourceSetMapFlags);
	GETPROC(cuGraphicsResourceGetMappedPointer);
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
	: m_outputBuffer({0})
	, m_outputBufferResource(nullptr)
{
	initializeCUDA();
	initializeCUVID();
}

void DecoderNV::initialize(std::shared_ptr<RendererInterface> renderer, int width, int height) 
{
	m_renderer    = renderer;
	m_frameWidth  = width;
	m_frameHeight = height;

	{
		unsigned int numDevices;
		if(CUFAILED(cuD3D11GetDevices(&numDevices, &m_device, 1, reinterpret_cast<ID3D11Device*>(renderer->getDevice()), CU_D3D11_DEVICE_LIST_ALL))) {
			throw std::runtime_error("D3D11 device is not CUDA capable");
		}
		if(CUFAILED(cuCtxCreate(&m_context, 0, m_device))) {
			throw std::runtime_error("Failed to create CUDA context");
		}
	}

	const cudaVideoCodec codecType = cudaVideoCodec_H264;
	const unsigned long numDecodeSurfaces = 20; // Worst case for H264.

	{
		CUVIDPARSERPARAMS params = {};
		params.CodecType = codecType;
		params.ulMaxNumDecodeSurfaces = numDecodeSurfaces;
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
		params.CodecType = codecType;
		params.ChromaFormat = cudaVideoChromaFormat_420;
		params.OutputFormat = cudaVideoSurfaceFormat_NV12;
		params.DeinterlaceMode = cudaVideoDeinterlaceMode_Weave;
		params.ulNumDecodeSurfaces = numDecodeSurfaces;
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
	if(m_outputBufferResource) {
		cuGraphicsUnregisterResource(m_outputBufferResource);
		m_renderer->releaseVideoBuffer(m_outputBuffer);
	}
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
}
	
int DecoderNV::onSequence(void* pThis, CUVIDEOFORMAT* format)
{
	return 1;
}
	
int DecoderNV::onDecode(void* pThis, CUVIDPICPARAMS* pic)
{
	DecoderNV* _this = reinterpret_cast<DecoderNV*>(pThis);
	if(CUFAILED(cuvidDecodePicture(_this->m_decoder, pic))) {
		throw std::runtime_error("Failed to decode picture frame");
	}
	
	CUdeviceptr frameDevicePtr;
	unsigned int framePitch;

	CUVIDPROCPARAMS procParams = {};
	procParams.progressive_frame = 1;
	if(cuvidMapVideoFrame(_this->m_decoder, pic->CurrPicIdx, &frameDevicePtr, &framePitch, &procParams) == CUDA_SUCCESS) {

		if(!_this->m_outputBufferResource) {
			_this->m_outputBuffer = _this->m_renderer->createVideoBuffer(SurfaceFormat::NV12, framePitch);
			if(CUFAILED(cuGraphicsD3D11RegisterResource(&_this->m_outputBufferResource, reinterpret_cast<ID3D11Resource*>(_this->m_outputBuffer.pResource), 0))) {
				throw std::runtime_error("Failed to register renderer output buffer resource with CUDA");
			}
			cuGraphicsResourceSetMapFlags(_this->m_outputBufferResource, CU_GRAPHICS_MAP_RESOURCE_FLAGS_WRITE_DISCARD);
		}
		
		const size_t frameNumBytes = _this->m_frameHeight * framePitch * 3 / 2;

		if(cuGraphicsMapResources(1, &_this->m_outputBufferResource, 0) == CUDA_SUCCESS) {
			CUdeviceptr outputDevicePtr = 0;
			size_t outputSize = 0;

			if(CUFAILED(cuGraphicsResourceGetMappedPointer(&outputDevicePtr, &outputSize, _this->m_outputBufferResource))) {
				throw std::runtime_error("Failed to get CUDA device pointer to the mapped output buffer");
			}
			assert(frameNumBytes == outputSize);
			if(CUFAILED(cuMemcpyDtoD(outputDevicePtr, frameDevicePtr, frameNumBytes))) {
				throw std::runtime_error("Failed to copy decoded video frame to the mapped output buffer");
			}
			cuGraphicsUnmapResources(1, &_this->m_outputBufferResource, 0);
		}

#if DEBUG_DUMP_YUV
		void* frameHostPtr;
		if(CUFAILED(cuMemAllocHost(&frameHostPtr, frameNumBytes))) {
			throw std::runtime_error("Failed to allocate host memory for dumping frame data");
		}
		cuMemcpyDtoH(frameHostPtr, frameDevicePtr, frameNumBytes);
		{
			std::ofstream dumpStream{"debug.yuv", std::ios::binary | std::ios::app};
			dumpStream.write(reinterpret_cast<const char*>(frameHostPtr), frameNumBytes);
		}
		cuMemFreeHost(frameHostPtr);
		std::fputc('.', stdout);
#endif

		cuvidUnmapVideoFrame(_this->m_decoder, frameDevicePtr);
	}
	else {
		throw std::runtime_error("Failed to map video frame memory");
	}

	return 1;
}
	
int DecoderNV::onDisplay(void* pThis, CUVIDPARSERDISPINFO* dispInfo)
{
	return 1;
}
