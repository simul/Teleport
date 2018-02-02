struct PixelInput
{
	float4 position : SV_POSITION;
	float2 texcoord : TEXCOORD;
};

Texture2D colorTexture : register(t0);
SamplerState defaultSampler : register(s0);

float4 main(PixelInput pin) : SV_TARGET
{
	return colorTexture.Sample(defaultSampler, pin.texcoord);
}
