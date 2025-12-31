struct PSInput
{
	float4 position : SV_POSITION;
	float2 uv : TEXCOORD;
};
 
cbuffer MVPBuffer : register(b0)
{
	float4x4 m_MVP;
};

cbuffer MaterialBuffer : register(b1)
{
	int textureID;
};

Texture2D g_Textures[2] : register(t0);
SamplerState g_sampler : register(s0);
SamplerState g_sampler1 : register(s1);
 
PSInput VSMain(float4 position : POSITION, float2 uv : TEXCOORD)
{
	PSInput result;
 
	result.position = mul(position, m_MVP);
	result.uv = uv;
 
	return result;
}
 
float4 PSMain(PSInput input) : SV_TARGET
{
	float4 color = g_Textures[textureID].Sample(g_sampler, input.uv);
	return color;
	// return g_texture.Sample(g_sampler, input.uv);
	// return g_Textures[textureID].Sample(g_sampler, input.uv);
	// return float4(input.uv.x, input.uv.y, 0, 1);
}