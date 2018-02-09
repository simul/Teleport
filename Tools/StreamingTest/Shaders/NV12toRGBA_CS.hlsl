// Copyright (c) 2018 Simul.co

cbuffer VideoConstants : register(b0)
{
	uint pitch;
};

Buffer<uint> pixels : register(t0);
RWTexture2D<float4> output : register(u0);

[numthreads(32, 32, 1)]
void main(uint3 ThreadID : SV_DispatchThreadID)
{
	uint frameWidth, frameHeight;
	output.GetDimensions(frameWidth, frameHeight);

	int YLocation = ThreadID.y * pitch + ThreadID.x;
	float Y = pixels.Load(YLocation) / 255.0;
	output[ThreadID.xy] = float4(Y, Y, Y, 1.0);
}
