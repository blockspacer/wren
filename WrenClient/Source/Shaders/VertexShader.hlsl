cbuffer cbPerObject : register(b0)
{
	matrix gWorldViewProj;
};

struct VertexIn
{
	float3 Pos : POSITION;
	//float4 Color : COLOR;
};

struct VertexOut
{
	float4 Pos : SV_POSITION;
	float4 Color : COLOR;
};

VertexOut main(VertexIn vin)
{
	VertexOut vout;

	// Transform to homogenous clip space
	vout.Pos = mul(float4(vin.Pos, 1.0f), gWorldViewProj);

	// Pass the color through without modification
	//vout.Color = vin.Color;
	vout.Color = float4(0.26f, 0.8f, 0.976f, 1.0f);

	return vout;
}