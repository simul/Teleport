// Copyright (c) 2018 Simul.co

cbuffer VideoConstants : register(b0)
{
	uint pitch;
};

static const float3x3 YUVtoRGB_ITU709 = { 
	1.0,  0.0,       1.5748,
	1.0, -0.187324, -0.468124,
	1.0,  1.8556,    0.0
};

Buffer<uint> pixels : register(t0);
RWTexture2D<float4> output : register(u0);

float3 YUVtoRGB(float3 YUV)
{
	YUV.gb -= 0.5;
	return mul(YUVtoRGB_ITU709, YUV);
}

float4 gatherY(uint2 p)
{
	uint4 loc = uint4(
		p.y     * pitch + p.x,
		p.y     * pitch + p.x + 1,
		(p.y+1) * pitch + p.x,
		(p.y+1) * pitch + p.x + 1);
	return float4(pixels.Load(loc.x), pixels.Load(loc.y), pixels.Load(loc.z), pixels.Load(loc.w)) / 255.0;
}

float2 gatherUV(uint size, uint2 p)
{
	uint2 loc = size + uint2(
		p.y/2 * pitch + p.x, 
		p.y/2 * pitch + p.x + 1);
	return float2(pixels.Load(loc.x), pixels.Load(loc.y)) / 255.0;
}

[numthreads(16, 16, 1)]
void main(uint3 ThreadID : SV_DispatchThreadID)
{
	uint frameWidth, frameHeight;
	output.GetDimensions(frameWidth, frameHeight);

	uint2 p   = 2 * ThreadID.xy;
	float4 Y  = gatherY(p);
	float2 UV = gatherUV(pitch * frameHeight, p);
	
	output[int2(p.x  , p.y  )] = float4(YUVtoRGB(float3(Y.x, UV.x, UV.y)), 1.0);
	output[int2(p.x+1, p.y  )] = float4(YUVtoRGB(float3(Y.y, UV.x, UV.y)), 1.0);
	output[int2(p.x  , p.y+1)] = float4(YUVtoRGB(float3(Y.z, UV.x, UV.y)), 1.0);
	output[int2(p.x+1, p.y+1)] = float4(YUVtoRGB(float3(Y.w, UV.x, UV.y)), 1.0);
}
