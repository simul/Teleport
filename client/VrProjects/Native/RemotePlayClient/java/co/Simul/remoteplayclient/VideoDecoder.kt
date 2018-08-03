// (C) Copyright 2018 Simul.co

package co.Simul.remoteplayclient

import android.graphics.SurfaceTexture
import android.media.MediaCodec
import android.media.MediaFormat
import android.util.Log
import android.view.Surface
import java.nio.ByteBuffer

enum class VideoCodec {
    H264,
    H265,
}

enum class PayloadType {
    FirstVCL,
    VCL,
    VPS,
    SPS,
    PPS,
    Other,
}

class VideoDecoder(private val mDecoderProxy: Long, private val mCodecTypeIndex: Int) : SurfaceTexture.OnFrameAvailableListener {

    private class DecoderState(private val mCodecType: VideoCodec) {
        private var hasVPS = false
        private var hasPPS = false
        private var hasSPS = false

        val isConfigured get() = when(mCodecType) {
            VideoCodec.H264 -> hasPPS && hasSPS
            VideoCodec.H265 -> hasVPS && hasPPS && hasSPS
        }
        fun update(payloadType: PayloadType) {
            when(payloadType) {
                PayloadType.VPS -> hasVPS = true
                PayloadType.PPS -> hasPPS = true
                PayloadType.SPS -> hasSPS = true
            }
        }
        fun reset() {
            hasVPS = false
            hasPPS = false
            hasSPS = false
        }
    }

    private val mDecoder = MediaCodec.createDecoderByType(mCodecMimeType)
    private val mDecoderState = DecoderState(mCodecType)
    private var mDecoderConfigured = false

    fun initialize(frameWidth: Int, frameHeight: Int, surface: SurfaceTexture) {
        if(mDecoderConfigured) {
            Log.e("RemotePlay", "VideoDecoder: Cannot initialize: already configured")
            return
        }

        val format = MediaFormat.createVideoFormat(mCodecMimeType, frameWidth, frameHeight)
        format.setInteger(MediaFormat.KEY_MAX_WIDTH, frameWidth)
        format.setInteger(MediaFormat.KEY_MAX_HEIGHT, frameHeight)

        surface.setOnFrameAvailableListener(this)
        mDecoder.configure(format, Surface(surface), null, 0)
        mDecoder.start()

        mDecoderConfigured = true
    }

    fun shutdown() {
        if(!mDecoderConfigured) {
            Log.e("RemotePlay", "VideoDecoder: Cannot shutdown: not configured")
            return
        }

        mDecoder.flush()
        mDecoder.stop()
        mDecoderState.reset()
        mDecoderConfigured = false
    }

    fun decode(buffer: ByteBuffer, payloadTypeIndex: Int) {
        if(!mDecoderConfigured) {
            Log.e("RemotePlay", "VideoDecoder: Cannot decode buffer: not configured")
            return
        }

        val payloadType = getPayloadTypeFromIndex(payloadTypeIndex)
        val payloadFlags = when(payloadType) {
            PayloadType.VPS -> MediaCodec.BUFFER_FLAG_CODEC_CONFIG
            PayloadType.PPS -> MediaCodec.BUFFER_FLAG_CODEC_CONFIG
            PayloadType.SPS -> MediaCodec.BUFFER_FLAG_CODEC_CONFIG
            else -> 0
        }
        val startCodes = when(payloadFlags) {
            MediaCodec.BUFFER_FLAG_CODEC_CONFIG -> byteArrayOf(0, 0, 0, 1)
            else -> byteArrayOf(0, 0, 1)
        }

        if(!mDecoderState.isConfigured && payloadFlags == 0) {
            return
        }
        mDecoderState.update(payloadType)

        if(payloadType == PayloadType.FirstVCL) {
            // Decode previous access unit.
            requestOutput()
        }

        val inputBuffer = startCodes.plus(ByteArray(buffer.remaining()))
        buffer.get(inputBuffer, startCodes.size, buffer.remaining())
        requestInput(inputBuffer, payloadFlags)
    }

    private fun requestInput(buffer: ByteArray, flags: Int) {
        val inputBufferID = mDecoder.dequeueInputBuffer(0)
        if(inputBufferID >= 0) {
            val inputBuffer = mDecoder.getInputBuffer(inputBufferID)
            inputBuffer.clear()
            inputBuffer.put(buffer)
            mDecoder.queueInputBuffer(inputBufferID, 0, buffer.size, 0, flags)
        }
        else {
            //Log.w("RemotePlay", "VideoDecoder: Could not dequeue decoder input buffer")
        }
    }

    private fun requestOutput() {
        var bufferInfo = MediaCodec.BufferInfo()
        val outputBufferID = mDecoder.dequeueOutputBuffer(bufferInfo, 0)
        if(outputBufferID >= 0) {
            mDecoder.releaseOutputBuffer(outputBufferID, true)
        }
        else {
            //Log.w("RemotePlay", "VideoDecoder: Could not dequeue decoder output buffer");
        }
    }

    external fun nativeFrameAvailable(decoderProxy: Long)
    override fun onFrameAvailable(surfaceTexture: SurfaceTexture?) {
        nativeFrameAvailable(mDecoderProxy)
    }

    private val mCodecType get() = when(mCodecTypeIndex) {
        1 -> VideoCodec.H264
        2 -> VideoCodec.H265
        else -> throw IllegalArgumentException("Invalid codec type index")
    }
    private val mCodecMimeType get() = when(mCodecType) {
        VideoCodec.H264 -> "video/avc"
        VideoCodec.H265 -> "video/hevc"
    }
    private fun getPayloadTypeFromIndex(payloadTypeIndex: Int) = when(payloadTypeIndex) {
        0 -> PayloadType.FirstVCL
        1 -> PayloadType.VCL
        2 -> PayloadType.VPS
        3 -> PayloadType.SPS
        4 -> PayloadType.PPS
        else -> PayloadType.Other
    }
}