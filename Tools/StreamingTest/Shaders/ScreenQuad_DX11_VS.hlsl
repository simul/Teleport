struct VertexOutput
{
	float4 position : SV_POSITION;
	float2 texcoord : TEXCOORD;
};

VertexOutput main(uint vertexID : SV_VertexId)
{
	VertexOutput vout;
	vout.texcoord = float2((vertexID << 1) & 2, vertexID & 2);
	vout.position = float4(vout.texcoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
	return vout;
}
