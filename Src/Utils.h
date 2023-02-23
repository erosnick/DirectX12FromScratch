#pragma once

#include <stdexcept>
#include <windows.h>
#include <d3d12.h>
#include <d3dx12.h>
#include <DirectXMath.h>
#include <DirectXColors.h>
#include <DirectXCollision.h>
#include <string>
#include <unordered_map>
#include <dxcapi.h>
#include <wrl.h>
#include "glm.h"

using namespace DirectX;
using namespace Microsoft;
using namespace Microsoft::WRL;

// 用于向上取整除法
#define ROUND_UP_DIV(A, B) (static_cast<uint32_t>(((A) + ((B) - 1)) / (B)))

// 更简洁的向上取整算法
#define ROUND_UP(A, B) (static_cast<uint32_t>(((A) + ((B) - 1)) &~ (B - 1)))

#define WIDE(x) L##x
#define WIDE1(x) WIDE(x)
#define FILE_NAME WIDE1(__FILE__)
#define FUNCTION_NAME WIDE1(__FUNCTION__)
#define ObjectName(object) L#object
#define CPU_DESCRIPTOR_HEAP_START(descriptorHeap) descriptorHeap->GetCPUDescriptorHandleForHeapStart()
#define GPU_DESCRIPTOR_HEAP_START(descriptorHeap) descriptorHeap->GetGPUDescriptorHandleForHeapStart()

extern ComPtr<IDxcUtils> dxcUtils;
extern ComPtr<IDxcCompiler3> dxcCompiler;
extern const uint32_t NumFrameResources;

inline void setD3D12DebugName(ID3D12Object * object, const std::wstring& name)
{
	object->SetName(name.c_str());
}

#define setD3D12DebugNameComPtr(object) setD3D12DebugName(object.Get(), ObjectName(object));

inline void setD3D12DebugNameIndexd(ID3D12Object* object, const std::wstring& name, uint32_t index)
{
	auto fullName = name + L"[" + std::to_wstring(index) + L"]";
	object->SetName(fullName.c_str());
}

/******************************************************************************************
Function:        TCHAR2STRING
Description:     TCHAR转string
Input:           str:待转化的TCHAR*类型字符串
Return:          转化后的string类型字符串
*******************************************************************************************/
inline std::string TCHAR2String(const TCHAR* wideString)
{
	std::string result;

	try
	{
		auto length = WideCharToMultiByte(CP_ACP, 0, wideString, -1, NULL, 0, NULL, NULL);

		char* temp = new char[length * sizeof(char)];

		WideCharToMultiByte(CP_ACP, 0, wideString, -1, temp, length, NULL, NULL);

		result = temp;
	}
	catch (std::exception e)
	{
	}

	return result;
}

class Utils
{
public:
	static UINT calculateConstantBufferByteSize(UINT byteSize)
	{
		// Constant buffers must be a multiple of the minimum hardware
		// allocation size (usually 256 bytes).  So round up to nearest
		// multiple of 256.  We do this by adding 255 and then masking off
		// the lower 2 bytes which store all bits < 256.
		// Example: Suppose byteSize = 300.
		// (300 + 255) & ~255
		// 555 & ~255
		// 0x022B & ~0x00ff
		// 0x022B & 0xff00
		// 0x0200
		// 512
		return (byteSize + 255) & ~255;
	}

	static ComPtr<ID3D12Resource> createDefaultBuffer(
		const ComPtr<ID3D12Device>& device,
		const ComPtr<ID3D12GraphicsCommandList>& commandList,
		const void* initData,
		UINT64 byteSize,
		ComPtr<ID3D12Resource>& uploadBuffer);

	static void createDXCCompiler();

	static void compileShader(const std::wstring& path, const std::wstring& entryPoint, const std::wstring& targetProfile, ComPtr<IDxcBlob>& shader);
	static ID3DBlob* loadShaderBinary(const std::string& path);
};

class DXException
{
public:
	DXException() = default;
	DXException(HRESULT hr, const std::wstring& inFunctionName, const std::wstring& inFilename, int inLineNumber, const std::wstring& inErrorMessage = L"");

	std::wstring ToWString() const;
	std::string ToString() const;

	HRESULT errorCode = S_OK;
	std::wstring functionName;
	std::wstring filename;
	int lineNumber = -1;
	std::wstring errorMessage;
};

#define DXThrow(result, message) throw DXException(result, FUNCTION_NAME, FILE_NAME, __LINE__, message);

#define DXCheck(result, message) if (FAILED(result)) { DXThrow(result, message); }

// Defines a subrange of geometry in a MeshGeometry.  This is for when multiple
// geometries are stored in one vertex and index buffer.  It provides the offsets
// and data needed to draw a subset of geometry stores in the vertex and index
// buffers so that we can implement the technique described by Figure 6.3.
struct SubmeshGeometry
{
	UINT indexCount = 0;
	UINT startIndexLocation = 0;
	INT baseVertexLocation = 0;

	// Bounding box of the geometry defined by this submesh.
	// This is used in later chapters of the book.
	DirectX::BoundingBox bounds;
};

struct MeshGeometry
{
	// Give it a name so we can look it up by name.
	std::string name;

	// System memory copies.  Use Blobs because the vertex/index format can be generic.
	// It is up to the client to cast appropriately.
	Microsoft::WRL::ComPtr<ID3DBlob> vertexBufferCPU = nullptr;
	Microsoft::WRL::ComPtr<ID3DBlob> indexBufferCPU = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Resource> vertexBufferGPU = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> indexBufferGPU = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Resource> vertexBufferUploader = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> indexBufferUploader = nullptr;

	// Data about the buffers.
	UINT vertexByteStride = 0;
	UINT vertexBufferByteSize = 0;
	DXGI_FORMAT indexFormat = DXGI_FORMAT_R16_UINT;
	UINT indexBufferByteSize = 0;

	// A MeshGeometry may store multiple geometries in one vertex/index buffer.
	// Use this container to define the Submesh geometries so we can draw
	// the Submeshes individually.
	std::unordered_map<std::string, SubmeshGeometry> drawArgs;

	D3D12_VERTEX_BUFFER_VIEW VertexBufferView() const
	{
		D3D12_VERTEX_BUFFER_VIEW vertexBufferView;
		vertexBufferView.BufferLocation = vertexBufferGPU->GetGPUVirtualAddress();
		vertexBufferView.StrideInBytes = vertexByteStride;
		vertexBufferView.SizeInBytes = vertexBufferByteSize;

		return vertexBufferView;
	}

	D3D12_INDEX_BUFFER_VIEW IndexBufferView() const
	{
		D3D12_INDEX_BUFFER_VIEW indexBufferView;
		indexBufferView.BufferLocation = indexBufferGPU->GetGPUVirtualAddress();
		indexBufferView.Format = indexFormat;
		indexBufferView.SizeInBytes = indexBufferByteSize;

		return indexBufferView;
	}

	// We can free this memory after we finish upload to the GPU.
	void DisposeUploaders()
	{
		vertexBufferUploader = nullptr;
		indexBufferUploader = nullptr;
	}
};

struct Light
{
	glm::vec3 strength = { 0.5f, 0.5f, 0.5f };
	float falloffStart = 1.0f;                          // point/spot light only
	glm::vec3 direction = { 0.0f, -1.0f, 0.0f };		// directional/spot light only
	float falloffEnd = 10.0f;                           // point/spot light only
	glm::vec3 position = { 0.0f, 0.0f, 0.0f };			// point/spot light only
	float spotPower = 64.0f;                            // spot light only
};

const uint32_t MaxLights = 16;

struct MaterialConstants
{
	glm::vec4 diffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
	glm::vec3 fresnelR0 = { 0.01f, 0.01f, 0.01f };
	float roughness = 0.25f;

	// Used in texture mapping.
	glm::mat4 transform = glm::mat4(1.0f);
};

// Simple struct to represent a material for our demos.  A production 3D engine
// would likely create a class hierarchy of Materials.
struct Material
{
	// Unique material name for lookup.
	std::string name;

	// Index into constant buffer corresponding to this material.
	int32_t materialConstantBufferIndex = -1;

	// Index into SRV heap for diffuse texture.
	int32_t diffuseSRVHeapIndex = -1;

	// Index into SRV heap for normal texture.
	int32_t normalSRVHeapIndex = -1;

	// Dirty flag indicating the material has changed and we need to update the constant buffer.
	// Because we have a material constant buffer for each FrameResource, we have to apply the
	// update to each FrameResource.  Thus, when we modify a material we should set 
	// NumFramesDirty = gNumFrameResources so that each frame resource gets the update.
	int32_t numFramesDirty = NumFrameResources;

	// Material constant buffer data used for shading.
	glm::vec4 diffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
	glm::vec3 fresnelR0 = { 0.01f, 0.01f, 0.01f };
	float roughness = 0.25f;
	glm::mat4 transform = glm::mat4(1.0f);
};

struct Texture
{
	// Unique material name for lookup.
	std::string name;

	std::wstring path;

	Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> uploadHeap = nullptr;
};