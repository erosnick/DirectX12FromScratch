struct PSInput
{
	float4 position : SV_POSITION;
	float2 uv : TEXCOORD;
};

cbuffer MaterialBuffer : register(b0)
{
	int textureID;
	int samplerID;
	int objectIndex;
	int padding;
};

struct SceneBuffer
{
    float4x4 viewProjectionMatrix;
};

StructuredBuffer<SceneBuffer> gObjects : register(t0);
// StructuredBuffer<MaterialBuffer> gMaterialss : register(t0);
 
PSInput VSMain(float4 position : POSITION, float2 uv : TEXCOORD, in uint in_vertex_index : SV_VertexID)
{
	PSInput result;
 
	// ConstantBuffer<SceneBuffer> sceneBuffer = ResourceDescriptorHeap[3];

	SceneBuffer sceneBuffer = gObjects[objectIndex];

	result.position = mul(position, sceneBuffer.viewProjectionMatrix);
	
	result.uv = uv;
 
	return result;
}
 
float4 PSMain(PSInput input) : SV_TARGET
{
	Texture2D texture = ResourceDescriptorHeap[textureID];

	SamplerState textureSampler = SamplerDescriptorHeap[samplerID];

	float4 color = texture.SampleLevel(textureSampler, input.uv, 0);
	
	return color;
}