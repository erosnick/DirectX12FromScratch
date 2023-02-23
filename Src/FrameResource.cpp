#include "pch.h"
#include "FrameResource.h"

FrameResource::FrameResource(ID3D12Device* device, uint32_t passCount, uint32_t objectCount, uint32_t materialCount, uint32_t waveVertexCount)
{
    DXCheck(device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(commandListAllocator.GetAddressOf())), L"CreateCommandAllocator failed!");

    passConstantBuffer = std::make_unique<UploadBuffer<PassConstants>>(device, passCount, true);
    objectConstantBuffer = std::make_unique<UploadBuffer<ObjectConstants>>(device, objectCount, true);
    materialConstantBuffer = std::make_unique<UploadBuffer<MaterialConstants>>(device, materialCount, true);

	wavesVertexBuffer = std::make_unique<UploadBuffer<DXVertex>>(device, waveVertexCount, false);
}

FrameResource::~FrameResource()
{

}