#include "pch.h"
#include "FrameResource.h"

FrameResource::FrameResource(ID3D12Device* device, UINT passCount, UINT objectCount)
{
    DXCheck(device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(commandListAllocator.GetAddressOf())), L"CreateCommandAllocator failed!");

    passConstantBuffer = std::make_unique<UploadBuffer<PassConstants>>(device, passCount, true);
    objectConstantBuffer = std::make_unique<UploadBuffer<ObjectConstants>>(device, objectCount, true);
}

FrameResource::~FrameResource()
{

}