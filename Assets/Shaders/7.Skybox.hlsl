struct VSInput
{
    float3 position : POSITION;
};

struct PSInput
{
    float3 worldPosition : POSITION;
    float4 position : SV_POSITION;
    float3 texcoord : TEXCOORD;
};

cbuffer ObjectConstants : register(b0)
{
    float4x4 model;
    float4x4 modelViewProjection;
};

cbuffer PassConstants : register(b1)
{
    float4x4 view;
    float4x4 inverseView;
    float4x4 projection;
    float4x4 inverseProjection;
    float4x4 viewProjection;
    float4x4 inverseViewProjection;
    float4x4 modelViewProjection1;
    float3 eyePositionW;
    float constantPerObjectPad;
    float2 renderTargetSize;
    float2 inverseRenderTargetSize;
    float nearZ;
    float farZ;
    float totalTime;
    float deltaTime;
};

TextureCube environmentTexture : register(t0);
SamplerState textureSampler : register(s0);

PSInput VSMain(VSInput input)
{
    PSInput result;

    result.worldPosition = mul(model, float4(input.position, 1.0f)).xyz;
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