// Defaults for number of lights.
#ifndef NUM_DIR_LIGHTS
    #define NUM_DIR_LIGHTS 4
#endif

#ifndef NUM_POINT_LIGHTS
    #define NUM_POINT_LIGHTS 0
#endif

#ifndef NUM_SPOT_LIGHTS
    #define NUM_SPOT_LIGHTS 0
#endif

// Include structures and functions for lighting.
#include "LightingUtil.hlsl"

struct VSInput
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 Texcoord : TEXCOORD;
    float4 Color : COLOR;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float3 WorldPosition : POSITION;
    float3 Normal : NORMAL;
    float2 Texcoord : TEXCOORD;
    float4 Color : COLOR;
};

cbuffer ObjectConstants : register(b0)
{
    float4x4 Model;
    float4x4 TextureTransform;
};

cbuffer MaterialConstants : register(b1)
{
	float4 DiffuseAlbedo;
    float3 FresnelR0;
    float  Roughness;
	float4x4 MaterialTransform;
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

Texture2D albedo : register(t0);
SamplerState textureSampler : register(s0);

PSInput VSMain(VSInput input)
{
    PSInput result;

    // result.position = mul(float4(input.position, 1.0f), testMatrix);
    result.WorldPosition = mul(Model, float4(input.Position, 1.0f)).xyz;
    result.Position = mul(ViewProjection, float4(result.WorldPosition, 1.0));
    result.Normal = mul(Model, float4(input.Normal, 0.0f)).xyz;
    result.Texcoord = input.Texcoord;
    float4 texcoord = mul(TextureTransform, float4(input.Texcoord, 0.0f, 1.0f));
    result.Texcoord = mul(MaterialTransform, texcoord).xy;
    result.Color = input.Color;

    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float4 diffuseAlbedo = albedo.Sample(textureSampler, input.Texcoord) * DiffuseAlbedo;

#ifdef ALPHA_TEST
	// Discard pixel if texture alpha < 0.1.  We do this test as soon 
	// as possible in the shader so that we can potentially exit the
	// shader early, thereby skipping the rest of the shader code.
	clip(diffuseAlbedo.a - 0.1f);
#endif

    float3 normal = normalize(input.Normal);

    float3 viewDirection = EyePosition - input.WorldPosition;

    float distToEye = length(viewDirection);
    viewDirection /= distToEye; // normalize

    // Indirect lighting.
    float4 ambient = ambientLight * diffuseAlbedo;

    const float shininess = 1.0f - Roughness;

    Material material = {diffuseAlbedo, FresnelR0, shininess};

    float3 shadowFactor = 1.0f;
    float4 directLight = ComputeLighting(lights, material, input.WorldPosition,
                                         normal, viewDirection, shadowFactor);

    float4 litColor = ambient + directLight;

#ifdef FOG
    float fogAmount = saturate((distToEye - FogStart) / FogRange);
    litColor = lerp(litColor, FogColor, fogAmount);
#endif

    // Common convention to take alpha from diffuse material.
    litColor.a = diffuseAlbedo.a;
    // litColor = float4(fogAmount, fogAmount, fogAmount, 1.0);
    // litColor = FogColor;

    return litColor;
}