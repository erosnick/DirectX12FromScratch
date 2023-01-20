struct PSInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD;
    float4 color : COLOR;
};

Texture2D albedo : register(t0);
SamplerState textureSampler : register(s0);

PSInput VSMain(float4 position : POSITION, float2 texcoord : TEXCOORD, float4 color : COLOR)
{
    PSInput result;

    result.position = position;
    result.texcoord = texcoord;
    result.color = color;

    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return albedo.Sample(textureSampler, input.texcoord);
}