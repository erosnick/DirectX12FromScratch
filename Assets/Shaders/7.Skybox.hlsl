// Include structures and functions for lighting.
#include "LightingUtil.hlsl"

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

cbuffer PassConstants : register(b2)
{
    float4x4 View;
    float4x4 InverseView;
    float4x4 Projection;
    float4x4 InverseProjection;
    float4x4 ViewProjection;
    float4x4 InverseViewProjection;
    float3 EyePosition;
    float ConstantPerObjectPad1;
    float2 RenderTargetSize;
    float2 InverseRenderTargetSize;
    float NearZ;
    float FarZ;
    float TotalTime;
    float DeltaTime;

    float4 ambientLight;

    // Allow application to change fog parameters once per frame.
    // For example, we may only use fog for certain times of day.
    float4 FogColor;
    float FogStart;
    float FogRange;
    float2 ConstantPerObjectPad2;

    // Indices [0, NUM_DIR_LIGHTS) are directional lights;
    // indices [NUM_DIR_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHTS) are point lights;
    // indices [NUM_DIR_LIGHTS+NUM_POINT_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHT+NUM_SPOT_LIGHTS)
    // are spot lights for a maximum of MaxLights per object.
    Light lights[MaxLights];
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

#ifdef FOG
    float distance = length(input.worldPosition) * 150.0f;

    float fogAmount = saturate((distance - FogStart) / FogRange);
    color = lerp(color, FogColor, fogAmount);
    color.a = 1.0f;
#endif

    // color = FogColor;
    // color = float4(input.texcoord, 1.0f);
    return color;
}