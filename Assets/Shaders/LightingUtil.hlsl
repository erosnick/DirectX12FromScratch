//***************************************************************************************
// LightingUtil.hlsl by Frank Luna (C) 2015 All Rights Reserved.
//
// Contains API for shader lighting.
//***************************************************************************************

#define MaxLights 16

struct Light
{
    float3 Strength;
    float FalloffStart; // point/spot light only
    float3 Direction;   // directional/spot light only
    float FalloffEnd;   // point/spot light only
    float3 Position;    // point light only
    float SpotPower;    // spot light only
};

struct Material
{
    float4 DiffuseAlbedo;
    float3 FresnelR0;
    float Shininess;
};

float CalculateAttenuation(float d, float falloffStart, float falloffEnd)
{
    // Linear falloff.
    return saturate((falloffEnd - d) / (falloffEnd - falloffStart));
}

// Schlick gives an approximation to Fresnel reflectance (see pg. 233 "Real-Time Rendering 3rd Ed.").
// R0 = ( (n-1)/(n+1) )^2, where n is the index of refraction.
float3 SchlickFresnel(float3 R0, float3 normal, float3 lightVec)
{
    float cosIncidentAngle = saturate(dot(normal, lightVec));

    float f0 = 1.0f - cosIncidentAngle;
    float3 reflectPercent = R0 + (1.0f - R0) * (f0 * f0 * f0 * f0 * f0);

    return reflectPercent;
}

float3 BlinnPhong(float3 lightStrength, float3 lightVector, float3 normal, float3 toEye, Material material)
{
    const float m = material.Shininess * 256.0f;
    float3 halfVector = normalize(toEye + lightVector);

    float roughnessFactor = (m + 8.0f) * pow(max(dot(halfVector, normal), 0.0f), m) / 8.0f;
    float3 fresnelFactor = SchlickFresnel(material.FresnelR0, halfVector, lightVector);

    float3 specularAlbedo = fresnelFactor * roughnessFactor;

    // Our specular formula goes outside [0,1] range, but we are 
    // doing LDR rendering.  So scale it down a bit.
    specularAlbedo = specularAlbedo / (specularAlbedo + 1.0f);

    return (material.DiffuseAlbedo.rgb + specularAlbedo) * lightStrength;
}

//---------------------------------------------------------------------------------------
// Evaluates the lighting equation for directional lights.
//---------------------------------------------------------------------------------------
float3 ComputeDirectionalLight(Light light, Material material, float3 normal, float3 toEye)
{
    // The light vector aims opposite the direction the light rays travel.
    float3 lightVector = normalize(-light.Direction);

    // Scale light down by Lambert's cosine law.
    float ndotl = max(dot(lightVector, normal), 0.0f);
    float3 lightStrength = light.Strength * ndotl;

    return BlinnPhong(lightStrength, lightVector, normal, toEye, material);
}

//---------------------------------------------------------------------------------------
// Evaluates the lighting equation for point lights.
//---------------------------------------------------------------------------------------
float3 ComputePointLight(Light light, Material material, float3 position, float3 normal, float3 toEye)
{
    // The vector from the surface to the light.
    float3 lightVector = light.Position - position;

    // The distance from surface to light.
    float d = length(lightVector);

    // Range test.
    if(d > light.FalloffEnd)
        return 0.0f;

    // Normalize the light vector.
    lightVector /= d;

    // Scale light down by Lambert's cosine law.
    float ndotl = max(dot(lightVector, normal), 0.0f);
    float3 lightStrength = light.Strength * ndotl;

    // Attenuate light by distance.
    float attenuation = CalculateAttenuation(d, light.FalloffStart, light.FalloffEnd);
    lightStrength *= attenuation;

    return BlinnPhong(lightStrength, lightVector, normal, toEye, material);
}

//---------------------------------------------------------------------------------------
// Evaluates the lighting equation for spot lights.
//---------------------------------------------------------------------------------------
float3 ComputeSpotLight(Light light, Material material, float3 position, float3 normal, float3 toEye)
{
    // The vector from the surface to the light.
    float3 lightVector = light.Position - position;

    // The distance from surface to light.
    float d = length(lightVector);

    // Range test.
    if (d > light.FalloffEnd)
        return 0.0f;

    // Normalize the light vector.
    lightVector /= d;

    // Scale light down by Lambert's cosine law.
    float ndotl = max(dot(lightVector, normal), 0.0f);
    float3 lightStrength = light.Strength * ndotl;

    // Attenuate light by distance.
    float attenuation = CalculateAttenuation(d, light.FalloffStart, light.FalloffEnd);
    lightStrength *= attenuation;

    // Scale by spotlight
    float spotFactor = pow(max(dot(-lightVector, light.Direction), 0.0f), light.SpotPower);
    lightStrength *= spotFactor;

    return BlinnPhong(lightStrength, lightVector, normal, toEye, material);
}

float4 ComputeLighting(Light lights[MaxLights], Material material,
                       float3 position, float3 normal, float3 toEye,
                       float3 shadowFactor)
{
    float3 result = 0.0f;

    int i = 0;

#if (NUM_DIR_LIGHTS > 0)
    for(i = 0; i < NUM_DIR_LIGHTS; ++i)
    {
        result += shadowFactor[i] * ComputeDirectionalLight(lights[i], material, normal, toEye);
    }
#endif

#if (NUM_POINT_LIGHTS > 0)
    for(i = NUM_DIR_LIGHTS; i < NUM_DIR_LIGHTS+NUM_POINT_LIGHTS; ++i)
    {
        result += ComputePointLight(lights[i], material, position, normal, toEye);
    }
#endif

#if (NUM_SPOT_LIGHTS > 0)
    for(i = NUM_DIR_LIGHTS + NUM_POINT_LIGHTS; i < NUM_DIR_LIGHTS + NUM_POINT_LIGHTS + NUM_SPOT_LIGHTS; ++i)
    {
        result += ComputeSpotLight(lights[i], material, position, normal, toEye);
    }
#endif 

    return float4(result, 0.0f);
}


