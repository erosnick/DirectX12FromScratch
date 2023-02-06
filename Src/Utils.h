#pragma once

#include <stdexcept>
#include <windows.h>
#include <d3d12.h>
#include <DirectXMath.h>
#include <DirectXColors.h>
#include <DirectXCollision.h>
#include <string>
#include <unordered_map>
#include <dxcapi.h>
#include <wrl.h>

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

class Utils
{
public:
	static UINT calcConstantBufferByteSize(UINT byteSize)
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

class DxException
{
public:
	DxException() = default;
	DxException(HRESULT hr, const std::wstring& inFunctionName, const std::wstring& inFilename, int inLineNumber, const std::wstring& inErrorMessage = L"");

	std::wstring ToString() const;

	HRESULT errorCode = S_OK;
	std::wstring functionName;
	std::wstring filename;
	int lineNumber = -1;
	std::wstring errorMessage;
};

#define DXThrow(result, message) throw DxException(result, FUNCTION_NAME, FILE_NAME, __LINE__, message);

#define DXCheck(result, message) if (FAILED(result)) { DXThrow(result, message); }

// Defines a subrange of geometry in a MeshGeometry.  This is for when multiple
// geometries are stored in one vertex and index buffer.  It provides the offsets
// and data needed to draw a subset of geometry stores in the vertex and index
// buffers so that we can implement the technique described by Figure 6.3.
struct SubmeshGeometry
{
	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	INT BaseVertexLocation = 0;

	// Bounding box of the geometry defined by this submesh.
	// This is used in later chapters of the book.
	DirectX::BoundingBox Bounds;
};

struct MeshGeometry
{
	// Give it a name so we can look it up by name.
	std::string Name;

	// System memory copies.  Use Blobs because the vertex/index format can be generic.
	// It is up to the client to cast appropriately.
	Microsoft::WRL::ComPtr<ID3DBlob> VertexBufferCPU = nullptr;
	Microsoft::WRL::ComPtr<ID3DBlob> IndexBufferCPU = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Resource> VertexBufferGPU = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> IndexBufferGPU = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Resource> VertexBufferUploader = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> IndexBufferUploader = nullptr;

	// Data about the buffers.
	UINT VertexByteStride = 0;
	UINT VertexBufferByteSize = 0;
	DXGI_FORMAT IndexFormat = DXGI_FORMAT_R16_UINT;
	UINT IndexBufferByteSize = 0;

	// A MeshGeometry may store multiple geometries in one vertex/index buffer.
	// Use this container to define the Submesh geometries so we can draw
	// the Submeshes individually.
	std::unordered_map<std::string, SubmeshGeometry> DrawArgs;

	D3D12_VERTEX_BUFFER_VIEW VertexBufferView() const
	{
		D3D12_VERTEX_BUFFER_VIEW vbv;
		vbv.BufferLocation = VertexBufferGPU->GetGPUVirtualAddress();
		vbv.StrideInBytes = VertexByteStride;
		vbv.SizeInBytes = VertexBufferByteSize;

		return vbv;
	}

	D3D12_INDEX_BUFFER_VIEW IndexBufferView() const
	{
		D3D12_INDEX_BUFFER_VIEW ibv;
		ibv.BufferLocation = IndexBufferGPU->GetGPUVirtualAddress();
		ibv.Format = IndexFormat;
		ibv.SizeInBytes = IndexBufferByteSize;

		return ibv;
	}

	// We can free this memory after we finish upload to the GPU.
	void DisposeUploaders()
	{
		VertexBufferUploader = nullptr;
		IndexBufferUploader = nullptr;
	}
};