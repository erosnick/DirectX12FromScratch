#pragma once

#include "Utils.h"
#include "MathHelper.h"
#include "UploadBuffer.h"

#include <memory>

#include "glm.h"

struct ObjectConstants
{
	glm::mat4 model;
	glm::mat4 modelViewProjection;
    glm::mat4 textureTransform;
};

struct PassConstants
{
    glm::mat4 view = glm::mat4(1.0f);
    glm::mat4 inverseView = glm::mat4(1.0f);
    glm::mat4 projection = glm::mat4(1.0f);
    glm::mat4 inverseProjection = glm::mat4(1.0f);
    glm::mat4 viewProjection = glm::mat4(1.0f);
    glm::mat4 inverseViewProjection = glm::mat4(1.0f);
    glm::vec3 eyePositionW = { 0.0f, 0.0f, 0.0f };
    float constantPerObjectPad = 0.0f;
    glm::vec2 renderTargetSize = { 0.0f, 0.0f };
    glm::vec2 inverseRenderTargetSize = { 0.0f, 0.0f };
    float nearZ = 0.0f;
    float farZ = 0.0f;
    float totalTime = 0.0f;
    float deltaTime = 0.0f;

    glm::vec4 ambientLight = { 0.0f, 0.0f, 0.0f, 1.0f };

	// Indices [0, NUM_DIR_LIGHTS) are directional lights;
	// indices [NUM_DIR_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHTS) are point lights;
	// indices [NUM_DIR_LIGHTS+NUM_POINT_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHT+NUM_SPOT_LIGHTS)
	// are spot lights for a maximum of MaxLights per object.
	Light lights[MaxLights];
};

// Stores the resources needed for the CPU to build the command lists
// for a frame.  
struct FrameResource
{
public:
    
    FrameResource(ID3D12Device* device, uint32_t passCount, uint32_t objectCount, uint32_t materialCount);
    FrameResource(const FrameResource& rhs) = delete;
    FrameResource& operator=(const FrameResource& rhs) = delete;
    ~FrameResource();

    // We cannot reset the allocator until the GPU is done processing the commands.
    // So each frame needs their own allocator.
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandListAllocator;

    // We cannot update a cbuffer until the GPU is done processing the commands
    // that reference it.  So each frame needs their own cbuffers.
    std::unique_ptr<UploadBuffer<PassConstants>> passConstantBuffer = nullptr;
	std::unique_ptr<UploadBuffer<MaterialConstants>> materialConstantBuffer = nullptr;
    std::unique_ptr<UploadBuffer<ObjectConstants>> objectConstantBuffer = nullptr;

    // Fence value to mark commands up to this fence point.  This lets us
    // check if these frame resources are still in use by the GPU.
    UINT64 fence = 0;
};