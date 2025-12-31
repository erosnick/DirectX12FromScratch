cbuffer cbPerObject : register( b0 )
{
    matrix g_mWorldViewProjection;
}

cbuffer MaterialBuffer : register(b1)
{
	int textureID;
	int samplerID;
    int objectIndex;
	int padding;
};
 
struct SkyboxVS_Input
{
    float4 Pos : POSITION;
};
 
struct SkyboxVS_Output
{
    float4 Pos : SV_POSITION;
    float3 Tex : TEXCOORD0;
};
 
SkyboxVS_Output VSMain( SkyboxVS_Input Input )
{
    SkyboxVS_Output Output;
    
    Output.Pos = Input.Pos;
    Output.Tex = normalize(mul(Input.Pos, g_mWorldViewProjection)).xyz;
    
    return Output;
}
 
float4 PSMain( SkyboxVS_Output Input ) : SV_TARGET
{
	TextureCube texture = ResourceDescriptorHeap[textureID];

	SamplerState textureSampler = SamplerDescriptorHeap[samplerID];

	float4 color = texture.Sample(textureSampler, Input.Tex);
    
    return color;
}