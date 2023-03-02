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

Texture2DArray treeMapArray : register(t0);
SamplerState textureSampler : register(s0);

cbuffer ObjectConstants : register(b0)
{
    float4x4 Model;
    float4x4 TextureTransform;
};

cbuffer MaterialConstants : register(b1)
{
    float4 DiffuseAlbedo;
    float3 FresnelR0;
    float Roughness;
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

struct VertexIn
{
    float3 WorldPosition : POSITION;
    float2 Size : SIZE; 
};

struct VertexOut
{
    float3 Center : POSITION;
    float2 Size : SIZE;
};

struct GeometryOut
{
    float4 Position : SV_POSITION;
    float3 WorldPosition : POSITION;
    float3 Normal : NORMAL;
    float2 Texcoord : TEXCOORD;
    uint PrimitiveID : SV_PrimitiveID;
};

VertexOut VSMain(VertexIn input)
{
    VertexOut Out;

    // 直接将数据传入几何着色器
    Out.Center = input.WorldPosition;
    Out.Size = input.Size;

    return Out;
}

// 由于我们要将每个点都扩展为一个四边形(即4个顶点)，因此每次调用几何着色器最多输出4个顶点
[maxvertexcount(4)]
void GSMain(point VertexOut input[1], uint primitiveID : SV_PrimitiveID, inout TriangleStream<GeometryOut> triangleStream)
{
    // 
    // 计算精灵的局部坐标系与世界空间的相对关系，以使公告牌与y轴对齐且面向观察者
    //
    float3 up = float3(0.0f, 1.0f, 0.0f);
    float3 look = EyePosition - input[0].Center;
    look.y = 0.0f;  // 与y轴对齐，以此使公告牌立于xy平面
    look = normalize(look);

    float3 right = cross(up, look);

    //
    // 计算世界空间中三角形带的顶点(即四边形)
    //
    float halfWidth = 0.5f * input[0].Size.x;
    float halfHeight = 0.5f * input[0].Size.y;

    float4 vertex[4];
    vertex[0] = float4(input[0].Center + halfWidth * right - halfHeight * up, 1.0f);
    vertex[1] = float4(input[0].Center + halfWidth * right + halfHeight * up, 1.0f);
    vertex[2] = float4(input[0].Center - halfWidth * right - halfHeight * up, 1.0f);
    vertex[3] = float4(input[0].Center - halfWidth * right + halfHeight * up, 1.0f);

    //
    // 将四边形的顶点变换到世界空间，并将它们以三角形带的形式输出
    //
    float2 texcoord[4] = 
    {
        float2(0.0f, 1.0f),
        float2(0.0f, 0.0f),
        float2(1.0f, 1.0f),
        float2(1.0f, 0.0f)
    };

    GeometryOut geometryOut;
    
    [unroll]
    for(int i = 0; i < 4; i++)
    {
        geometryOut.Position = mul(ViewProjection, vertex[i]);
        geometryOut.WorldPosition = vertex[i].xyz;
        geometryOut.Normal = look;
        geometryOut.Texcoord = texcoord[i];
        geometryOut.PrimitiveID = primitiveID;

        triangleStream.Append(geometryOut);
    }
}

float4 PSMain(GeometryOut input) : SV_Target
{
    float3 uvw = float3(input.Texcoord, input.PrimitiveID % 3);
    float4 diffuseAlbedo = treeMapArray.Sample(textureSampler, uvw) * DiffuseAlbedo;

#ifdef ALPHA_TEST
    // 忽略纹理alpha值 < 0.1的像素。这个测试要尽早完成，以便提前退出着色器，
    // 使满足条件的像素跳过不必要的着色器后续处理
    clip(diffuseAlbedo.a - 0.1f);
#endif

    // Interpolating normal can unnormalize it, so renormalize it.
    input.Normal = normalize(input.Normal);

    // Vector from point being lit to eye.
    float3 toEyeW = EyePosition - input.WorldPosition;
    float distToEye = length(toEyeW);
    toEyeW /= distToEye; // normalize

    // Light terms.
    float4 ambient = ambientLight * diffuseAlbedo;

    const float shininess = 1.0f - Roughness;
    Material material = {diffuseAlbedo, FresnelR0, shininess};
    float3 shadowFactor = 1.0f;
    float4 directLight = ComputeLighting(lights, material, input.WorldPosition,
                                         input.Normal, toEyeW, shadowFactor);

    float4 litColor = ambient + directLight;

#ifdef FOG
    float fogAmount = saturate((distToEye - FogStart) / FogRange);
    litColor = lerp(litColor, FogColor, fogAmount);
#endif

    // Common convention to take alpha from diffuse albedo.
    litColor.a = diffuseAlbedo.a;

    return litColor;
}