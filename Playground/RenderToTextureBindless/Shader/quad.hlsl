struct PSInput
{
	float4 m_v4Position : SV_POSITION;
	float4 m_v4Color :COLOR;
	float2 m_v2UV : TEXCOORD;
};

cbuffer MVOBuffer : register(b0)
{
	float4x4 m_MVO;
};

SamplerState g_sampler : register(s0);

static const float4x4 g_MVP =
{
    1.810660,    0.000000,     0.000000,       0.000000,
    0.000000,    2.414214,     0.000000,       0.000000,
    0.000000,    0.000000,    1.002,      	1.000000,
    0.000000,    0.000000,    19.83984,   20.000000
};

PSInput VSMain(float4 v4Position : POSITION, float4 v4Color : COLOR, float2 v2UV : TEXCOORD)
{
	PSInput result;

	// result.m_v4Position = mul(v4Position, g_MVP);
	result.m_v4Position = mul(v4Position, m_MVO);
	// result.m_v4Position = v4Position;
	result.m_v4Color = v4Color;
	result.m_v2UV = float2(v2UV.x, 1.0 - v2UV.y);

	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	Texture2D texture = ResourceDescriptorHeap[0];

	SamplerState textureSampler = SamplerDescriptorHeap[0];

	return texture.Sample(textureSampler, input.m_v2UV);
	// return float4(1.0f, 0.0f, 0.0f, 1.0f);
}
