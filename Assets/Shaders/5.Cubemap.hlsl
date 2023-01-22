struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD;
    float4 color : COLOR;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD;
    float4 color : COLOR;
};

cbuffer ModelViewProjectionBuffer : register(b0)
{
    float4x4 model;
    float4x4 modelViewProjection;
};

Texture2D albedo : register(t0);
SamplerState textureSampler : register(s0);

PSInput VSMain(VSInput input)
{
    PSInput result;

    result.position = mul(float4(input.position, 1.0f), modelViewProjection);
    result.normal = mul(float4(input.normal, 0.0f), model);
    result.texcoord = input.texcoord;
    result.color = input.color;

    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float3 n = normalize(input.normal);

    float3 lightDir1 = normalize(float3(-1.0f, -1.0f, 1.0f));
    float3 lightDir2 = normalize(float3(1.0f, -1.0f, 1.0f));

    float diffuse1 = max(0.0f, dot(n, -lightDir1));
    float diffuse2 = max(0.0f, dot(n, -lightDir2));

    float3 color = albedo.Sample(textureSampler, input.texcoord).rgb;

    color = float3(1.0f, 1.0f, 1.0f);

    return float4(color * (diffuse1 + diffuse2) * 0.5f, 1.0f);
}