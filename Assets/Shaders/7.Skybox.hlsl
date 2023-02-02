struct VSInput
{
    float3 position : POSITION;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float3 texcoord : TEXCOORD;
};

cbuffer ModelViewProjectionBuffer : register(b0)
{
    float4x4 model;
    float4x4 modelViewProjection;
};

TextureCube environmentTexture : register(t0);
SamplerState textureSampler : register(s0);

PSInput VSMain(VSInput input)
{
    PSInput result;

    result.position = mul(modelViewProjection, float4(input.position, 1.0f));
    result.texcoord = input.position;

    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float4 color = environmentTexture.Sample(textureSampler, input.texcoord);
    // color = float4(input.texcoord, 1.0f);
    return color;
}