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
 
PSInput VSMain(float4 position : POSITION, float2 uv : TEXCOORD, in uint in_vertex_index : SV_VertexID)
{
	PSInput result;
 
	result.position = mul(position, m_MVP);
	result.uv = uv;
 
	return result;
}
 
float4 PSMain(PSInput input) : SV_TARGET
{
	Texture2D texture = ResourceDescriptorHeap[textureID];

	// float4 color = g_Textures[textureID].Sample(g_sampler, input.uv);
	float4 color = texture.SampleLevel(g_sampler, input.uv, 0);
	// float4 color = texture.Sample(g_sampler, input.uv);
	return color;
	// return g_texture.Sample(g_sampler, input.uv);
	// return g_Textures[textureID].Sample(g_sampler, input.uv);
	// return float4(input.uv.x, input.uv.y, 0, 1);
}