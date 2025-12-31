//***************************************************************************************
// Init Direct3D.cpp by Frank Luna (C) 2015 All Rights Reserved.
//
// Demonstrates the sample framework by initializing Direct3D, clearing 
// the screen, and displaying frame stats.
//
//***************************************************************************************

#include "../Common/d3dApp.h"
#include <DirectXColors.h>
#include <strsafe.h>
#include <tchar.h>
#include <fstream>

//for WIC
#include <wincodec.h>

#include "../Common/DDSTextureLoader12.h"

#define GLM_FORCE_LEFT_HANDED
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include "../Common/glm/glm.hpp"
#include "../Common/glm/vec3.hpp" // float3
#include "../Common/glm/vec4.hpp" // float4
#include "../Common/glm/mat4x4.hpp" // glm::mat4
#include "../Common/glm/ext/matrix_transform.hpp" 	// glm::translate, glm::rotate, glm::scale
#include "../Common/glm/ext/matrix_clip_space.hpp" 	// glm::perspective
#include "../Common/glm/ext/scalar_constants.hpp" 	// glm::pi
#include "../Common/glm/gtx/euler_angles.hpp"		// glm::yawPitchRoll

#include "../Common/GeometryGenerator.h"
#include <dxcapi.h>

//linker
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxcompiler.lib")

using namespace DirectX;
using namespace Microsoft;
using namespace Microsoft::WRL;

//新定义的宏用于上取整除法
#define GRS_UPPER_DIV(A,B) ((uint32_t)(((A)+((B)-1))/(B)))

//更简洁的向上边界对齐算法 内存管理中常用 请记住
#define GRS_UPPER(A,B) ((uint32_t)(((A)+((B)-1))&~(B - 1)))

#define KiB(num) ((num) << 10ull)
#define MiB(num) ((num) << 20ull)
#define GiB(num) ((num) << 30ull)

#define COM_SAFE_RELEASE(obj) ((obj) ? (obj)->Release(), (obj) = nullptr, true : false)

#define ArrayCount(arr) (sizeof(arr) / sizeof((arr)[0]))
#define ZeroStruct(to_zero) memset(to_zero, 0, sizeof(*(to_zero)))

#define STRINGIFY__(x) #x
#define STRINGIFY_(x)  STRINGIFY__(x)
#define STRINGIFY(x)   STRINGIFY_(x)

using float2 = glm::vec2;
using float3 = glm::vec3;
using float4 = glm::vec4;
using float4x4 = glm::mat4;

inline float4x4 Identity4x4()
{
	return float4x4(1.0f);
}

struct Transform
{
    float3 position {0, 0, 0};
    float3 rotation {0, 0, 0}; // radians, XYZ
    float3 scale    {1, 1, 1};

    float4x4 WorldMatrix() const
    {
        float4x4 T = glm::translate(float4x4(1.0f), position);
        float4x4 R = glm::yawPitchRoll(
            rotation.y, rotation.x, rotation.z);
        float4x4 S = glm::scale(glm::mat4(1.0f), scale);

        return T * R * S;
    }
};

enum D3D12_RootParameters
{
	D3D12_RootParameter_32bit_constants,
	D3D12_RootParameter_pass_cbv,
	D3D12_RootParameter_view_cbv,
	D3D12_RootParameter_global_cbv,
	D3D12_RootParameter_COUNT,
};

ID3D12Resource *D3D12_CreateUploadBuffer(
	ID3D12Device  *device,
	uint32_t       size,
	const wchar_t *debug_name,
	const void    *initial_data      = nullptr,
	uint32_t       initial_data_size = 0);

ComPtr<ID3D12RootSignature>	mRootSignatureBindless;

class CGRSCOMException
{
public:
	CGRSCOMException(HRESULT hr) : m_hrError(hr)
	{
	}
	HRESULT Error() const
	{
		return m_hrError;
	}
private:
	const HRESULT m_hrError;
};


struct Vertex
{
	float3 m_vPos;		//Position
	float2 m_vTex;		//Texcoord
	float3 m_vNor;		//Normal
};

struct Vertex2D
{
	float2 m_vPos;		//Position
	float2 m_vTex;		//Texcoord
	float4 m_vColor;		//Color
};

struct VertexQuad
{
	float4 m_vPos;		//Position
	float4 m_vColor;		//Color
	float2 m_vTex;		//Texcoord
};

struct SkyboxVertex
{
	//天空盒子的顶点结构
	float4 m_vPos;
};

struct FrameMVPBuffer
{
	float4x4 m_MVP;	//经典的Model-view-projection(MVP)矩阵.
	float4x4 m_mWorld;
	uint32_t materialIndex;
};

struct FrameMVOBuffer
{
	float4x4 m_MVO;	//经典的Model-view-projection(MVP)矩阵.
};

struct alignas(16) SceneBuffer
{
	float4x4 viewProjectionMatrix;
};

struct MaterialBuffer
{
	uint32_t textureID;
	uint32_t samplerID;
	uint32_t objectIndex;
	uint32_t padding;
};

struct D3D12_PassConstants
{
	uint32_t vbuffer_srv;
};

struct D3D12_RootConstants
{
	float2 offset;
	uint32_t texture_index;
};

struct Mesh
{
	// 几何资源（所有权）
    ComPtr<ID3D12Resource> vertexBuffer;
    ComPtr<ID3D12Resource> indexBuffer;

	// Views（只读描述）
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView;
    D3D12_INDEX_BUFFER_VIEW  indexBufferView;
    
	uint32_t indexCount;
    uint32_t vertexCount;
};

struct RenderItem
{
    uint32_t objectIndex;
    uint32_t meshID;
    uint32_t materialIndex;
};

class Actor
{
public:
    Transform transform;
    RenderItem renderData;
};

//------------------------------------------------------------------------
// DXC

struct DXC_State
{
	IDxcCompiler3 *compiler;
};

DXC_State g_dxc;

void DXC_Init()
{
	HRESULT hr;

	hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&g_dxc.compiler));
	ThrowIfFailed(hr);
}

bool DXC_CompileShader(
	const char    *source,
	uint32_t       source_size,
	const wchar_t *entry_point,
	const wchar_t *target,
	IDxcBlob     **result_blob,
	IDxcBlob     **error_blob)
{
	assert(g_dxc.compiler || !"Call DXC_Init before calling DXC_CompileShader");

	bool result = false;

	const wchar_t *args[] = {
		L"-E", entry_point,
		L"-T", target,
		L"-WX",
		L"-Zi",
	};

	DxcBuffer source_buffer = {
		.Ptr      = source,
		.Size     = source_size,
		.Encoding = 0,
	};

	HRESULT hr;

	IDxcResult *compile_result = nullptr;
	hr = g_dxc.compiler->Compile(&source_buffer, args, ArrayCount(args), nullptr, IID_PPV_ARGS(&compile_result));

	if (SUCCEEDED(hr))
	{
		HRESULT compile_hr;
		hr = compile_result->GetStatus(&compile_hr);

		if (SUCCEEDED(compile_hr))
		{
			if (compile_result->HasOutput(DXC_OUT_OBJECT))
			{
				hr = compile_result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(result_blob), nullptr);
				ThrowIfFailed(hr);

				result = true;
			}
		}

		// even if we succeeded, we could have warnings (if I didn't pass WX above...)
		if (compile_result->HasOutput(DXC_OUT_ERRORS))
		{
			hr = compile_result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(error_blob), nullptr);
			ThrowIfFailed(hr);
		}
	}

	return result;
}

struct WICTranslate
{
	GUID wic;
	DXGI_FORMAT format;
};

struct ShaderData
{
	std::vector<uint8_t> vsData;
	std::vector<uint8_t> psData;
};

struct D3D12_BufferAllocation
{
	ID3D12Resource           *buffer;
	void                     *cpu_base;
	D3D12_GPU_VIRTUAL_ADDRESS gpu_base;
	uint32_t                  offset;
};

struct D3D12_LinearAllocator
{
	ID3D12Resource           *buffer;
	char                     *cpu_base;
	D3D12_GPU_VIRTUAL_ADDRESS gpu_base;
	uint32_t                  at;
	uint32_t                  capacity;

	void Init(ID3D12Device *device, uint32_t size)
	{
		buffer = D3D12_CreateUploadBuffer(device, size, L"Frame Allocator");

		void *mapped;

		D3D12_RANGE null_range = {};
		HRESULT hr = buffer->Map(0, &null_range, &mapped);
		ThrowIfFailed(hr);

		cpu_base = (char *)mapped;
		gpu_base = buffer->GetGPUVirtualAddress();
		at       = 0;
		capacity = size;
	}

	D3D12_BufferAllocation Allocate(uint32_t size, uint32_t align)
	{
		// evil bit hack: round up to the next multiple of `align` so long as `align` is a power of 2
		uint32_t at_aligned = (at + (align - 1)) & (-(int32_t)align);
		assert(at_aligned <= capacity);

		D3D12_BufferAllocation result = {
			.buffer   = buffer,
			.cpu_base = cpu_base + at_aligned,
			.gpu_base = gpu_base + at_aligned,
			.offset   = at_aligned,
		};

		at = at_aligned + size;

		return result;
	}

	void Reset()
	{
		at = 0;
	}

	void Release()
	{
		buffer->Unmap(0, nullptr);
		buffer->Release();
		ZeroStruct(this);
	}
};

struct D3D12_Descriptor
{
	D3D12_CPU_DESCRIPTOR_HANDLE cpu;
	D3D12_GPU_DESCRIPTOR_HANDLE gpu;
	uint32_t                    index;
};

struct D3D12_DescriptorAllocator
{
	ID3D12DescriptorHeap       *heap;
	D3D12_DESCRIPTOR_HEAP_TYPE  type;
	D3D12_CPU_DESCRIPTOR_HANDLE cpu_base;
	D3D12_GPU_DESCRIPTOR_HANDLE gpu_base;
	uint32_t                    stride;
	uint32_t                    at;
	uint32_t                    capacity;

	void Init(ID3D12Device *device, D3D12_DESCRIPTOR_HEAP_TYPE in_type, uint32_t in_capacity, bool shader_visible, const wchar_t *debug_name)
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {
			.Type           = in_type,
			.NumDescriptors = in_capacity,
			.Flags          = shader_visible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
		};

		HRESULT hr;

		hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap));
		ThrowIfFailed(hr);

		heap->SetName(debug_name);

		cpu_base = heap->GetCPUDescriptorHandleForHeapStart();

		if (shader_visible)
		{
			gpu_base = heap->GetGPUDescriptorHandleForHeapStart();
		}

		stride = device->GetDescriptorHandleIncrementSize(in_type);

		type     = in_type;
		at       = 0;
		capacity = in_capacity;
	}

	D3D12_Descriptor Allocate()
	{
		assert(at < capacity);

		uint32_t index = at++;

		D3D12_Descriptor result = {
			.cpu   = { cpu_base.ptr + stride*index },
			.gpu   = { gpu_base.ptr + stride*index },
			.index = index,
		};

		return result;
	}

	void Reset()
	{
		at = 0;
	}

	void Release()
	{
		heap->Release();
		ZeroStruct(this);
	}
};

struct TriangleGuy
{
	float2 position;
	uint32_t texture;
};

struct D3D12_Scene
{
	bool initialized;

	ID3D12PipelineState *pso;

	ID3D12Resource *ibuffer;
	ID3D12Resource *vbuffer;

	D3D12_Descriptor vbuffer_srv;

	ID3D12Resource  *textures     [4];
	D3D12_Descriptor textures_srvs[4];

	uint32_t    triangle_guy_count;
	TriangleGuy triangle_guys[16];

	uint32_t    texture_index_offset = 0;
};

struct D3D12_State
{
	IDXGIFactory6       *factory;
	IDXGIAdapter1       *adapter;
	ID3D12Device        *device;
	ID3D12CommandQueue  *queue;
	ID3D12CommandAllocator* allocator;
	ID3D12Fence         *fence;
	ID3D12RootSignature *rs_bindless;

	uint64_t frame_index;

	D3D12_DescriptorAllocator cbv_srv_uav;
	D3D12_DescriptorAllocator rtv;

	IDXGISwapChain1 *swap_chain;
	int window_w;
	int window_h;
};

D3D12_State g_d3d;

struct RenderTexture
{
	void begin(const ComPtr<ID3D12GraphicsCommandList>& commandList, int clientWidth, int clientHeight)
	{
		auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(renderTarget.Get(),
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

		commandList->ResourceBarrier(1, &barrier);

		D3D12_CPU_DESCRIPTOR_HANDLE stRTVHandle = DHRTVTexture->GetCPUDescriptorHandleForHeapStart();
		D3D12_CPU_DESCRIPTOR_HANDLE stDSVHandle = DHDSVTexture->GetCPUDescriptorHandleForHeapStart();

		//设置渲染目标
		commandList->OMSetRenderTargets(1, &stRTVHandle, FALSE, &stDSVHandle);

		D3D12_VIEWPORT screenViewport = 
		{
			.TopLeftX = 0,
			.TopLeftY = 0,
			.Width    = static_cast<float>(clientWidth),
			.Height   = static_cast<float>(clientHeight),
			.MinDepth = 0.0f,
			.MaxDepth = 1.0f,
		};

		D3D12_RECT scissorRect = 
		{
			.left = 0,
			.top = 0,
			.right = clientWidth,
			.bottom = clientHeight,
		};

		commandList->RSSetViewports(1, &screenViewport);
		commandList->RSSetScissorRects(1, &scissorRect);

		commandList->ClearRenderTargetView(stRTVHandle, clearColor, 0, nullptr);
		commandList->ClearDepthStencilView(stDSVHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
	}

	void end(const ComPtr<ID3D12GraphicsCommandList>& commandList)
	{
		//渲染完毕将渲染目标纹理由渲染目标状态转换回纹理状态，并准备显示
		auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(renderTarget.Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

		commandList->ResourceBarrier(1, &barrier);
	}

	ComPtr<ID3D12DescriptorHeap> DHRTVTexture;
	ComPtr<ID3D12DescriptorHeap> DHDSVTexture;
	ComPtr<ID3D12Resource> renderTarget;
	ComPtr<ID3D12Resource> depthStencilTexture;

	float clearColor[4] = { 0.8f, 0.8f, 0.8f, 0.0f };
};

static WICTranslate g_WICFormats[] =
{//WIC格式与DXGI像素格式的对应表，该表中的格式为被支持的格式
	{ GUID_WICPixelFormat128bppRGBAFloat,       DXGI_FORMAT_R32G32B32A32_FLOAT },

	{ GUID_WICPixelFormat64bppRGBAHalf,         DXGI_FORMAT_R16G16B16A16_FLOAT },
	{ GUID_WICPixelFormat64bppRGBA,             DXGI_FORMAT_R16G16B16A16_UNORM },

	{ GUID_WICPixelFormat32bppRGBA,             DXGI_FORMAT_R8G8B8A8_UNORM },
	{ GUID_WICPixelFormat32bppBGRA,             DXGI_FORMAT_B8G8R8A8_UNORM }, // DXGI 1.1
	{ GUID_WICPixelFormat32bppBGR,              DXGI_FORMAT_B8G8R8X8_UNORM }, // DXGI 1.1

	{ GUID_WICPixelFormat32bppRGBA1010102XR,    DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM }, // DXGI 1.1
	{ GUID_WICPixelFormat32bppRGBA1010102,      DXGI_FORMAT_R10G10B10A2_UNORM },

	{ GUID_WICPixelFormat16bppBGRA5551,         DXGI_FORMAT_B5G5R5A1_UNORM },
	{ GUID_WICPixelFormat16bppBGR565,           DXGI_FORMAT_B5G6R5_UNORM },

	{ GUID_WICPixelFormat32bppGrayFloat,        DXGI_FORMAT_R32_FLOAT },
	{ GUID_WICPixelFormat16bppGrayHalf,         DXGI_FORMAT_R16_FLOAT },
	{ GUID_WICPixelFormat16bppGray,             DXGI_FORMAT_R16_UNORM },
	{ GUID_WICPixelFormat8bppGray,              DXGI_FORMAT_R8_UNORM },

	{ GUID_WICPixelFormat8bppAlpha,             DXGI_FORMAT_A8_UNORM },
};

// WIC 像素格式转换表.
struct WICConvert
{
	GUID source;
	GUID target;
};

static WICConvert g_WICConvert[] =
{
	// 目标格式一定是最接近的被支持的格式
	{ GUID_WICPixelFormatBlackWhite,            GUID_WICPixelFormat8bppGray }, // DXGI_FORMAT_R8_UNORM

	{ GUID_WICPixelFormat1bppIndexed,           GUID_WICPixelFormat32bppRGBA }, // DXGI_FORMAT_R8G8B8A8_UNORM
	{ GUID_WICPixelFormat2bppIndexed,           GUID_WICPixelFormat32bppRGBA }, // DXGI_FORMAT_R8G8B8A8_UNORM
	{ GUID_WICPixelFormat4bppIndexed,           GUID_WICPixelFormat32bppRGBA }, // DXGI_FORMAT_R8G8B8A8_UNORM
	{ GUID_WICPixelFormat8bppIndexed,           GUID_WICPixelFormat32bppRGBA }, // DXGI_FORMAT_R8G8B8A8_UNORM

	{ GUID_WICPixelFormat2bppGray,              GUID_WICPixelFormat8bppGray }, // DXGI_FORMAT_R8_UNORM
	{ GUID_WICPixelFormat4bppGray,              GUID_WICPixelFormat8bppGray }, // DXGI_FORMAT_R8_UNORM

	{ GUID_WICPixelFormat16bppGrayFixedPoint,   GUID_WICPixelFormat16bppGrayHalf }, // DXGI_FORMAT_R16_FLOAT
	{ GUID_WICPixelFormat32bppGrayFixedPoint,   GUID_WICPixelFormat32bppGrayFloat }, // DXGI_FORMAT_R32_FLOAT

	{ GUID_WICPixelFormat16bppBGR555,           GUID_WICPixelFormat16bppBGRA5551 }, // DXGI_FORMAT_B5G5R5A1_UNORM

	{ GUID_WICPixelFormat32bppBGR101010,        GUID_WICPixelFormat32bppRGBA1010102 }, // DXGI_FORMAT_R10G10B10A2_UNORM

	{ GUID_WICPixelFormat24bppBGR,              GUID_WICPixelFormat32bppRGBA }, // DXGI_FORMAT_R8G8B8A8_UNORM
	{ GUID_WICPixelFormat24bppRGB,              GUID_WICPixelFormat32bppRGBA }, // DXGI_FORMAT_R8G8B8A8_UNORM
	{ GUID_WICPixelFormat32bppPBGRA,            GUID_WICPixelFormat32bppRGBA }, // DXGI_FORMAT_R8G8B8A8_UNORM
	{ GUID_WICPixelFormat32bppPRGBA,            GUID_WICPixelFormat32bppRGBA }, // DXGI_FORMAT_R8G8B8A8_UNORM

	{ GUID_WICPixelFormat48bppRGB,              GUID_WICPixelFormat64bppRGBA }, // DXGI_FORMAT_R16G16B16A16_UNORM
	{ GUID_WICPixelFormat48bppBGR,              GUID_WICPixelFormat64bppRGBA }, // DXGI_FORMAT_R16G16B16A16_UNORM
	{ GUID_WICPixelFormat64bppBGRA,             GUID_WICPixelFormat64bppRGBA }, // DXGI_FORMAT_R16G16B16A16_UNORM
	{ GUID_WICPixelFormat64bppPRGBA,            GUID_WICPixelFormat64bppRGBA }, // DXGI_FORMAT_R16G16B16A16_UNORM
	{ GUID_WICPixelFormat64bppPBGRA,            GUID_WICPixelFormat64bppRGBA }, // DXGI_FORMAT_R16G16B16A16_UNORM

	{ GUID_WICPixelFormat48bppRGBFixedPoint,    GUID_WICPixelFormat64bppRGBAHalf }, // DXGI_FORMAT_R16G16B16A16_FLOAT
	{ GUID_WICPixelFormat48bppBGRFixedPoint,    GUID_WICPixelFormat64bppRGBAHalf }, // DXGI_FORMAT_R16G16B16A16_FLOAT
	{ GUID_WICPixelFormat64bppRGBAFixedPoint,   GUID_WICPixelFormat64bppRGBAHalf }, // DXGI_FORMAT_R16G16B16A16_FLOAT
	{ GUID_WICPixelFormat64bppBGRAFixedPoint,   GUID_WICPixelFormat64bppRGBAHalf }, // DXGI_FORMAT_R16G16B16A16_FLOAT
	{ GUID_WICPixelFormat64bppRGBFixedPoint,    GUID_WICPixelFormat64bppRGBAHalf }, // DXGI_FORMAT_R16G16B16A16_FLOAT
	{ GUID_WICPixelFormat48bppRGBHalf,          GUID_WICPixelFormat64bppRGBAHalf }, // DXGI_FORMAT_R16G16B16A16_FLOAT
	{ GUID_WICPixelFormat64bppRGBHalf,          GUID_WICPixelFormat64bppRGBAHalf }, // DXGI_FORMAT_R16G16B16A16_FLOAT

	{ GUID_WICPixelFormat128bppPRGBAFloat,      GUID_WICPixelFormat128bppRGBAFloat }, // DXGI_FORMAT_R32G32B32A32_FLOAT
	{ GUID_WICPixelFormat128bppRGBFloat,        GUID_WICPixelFormat128bppRGBAFloat }, // DXGI_FORMAT_R32G32B32A32_FLOAT
	{ GUID_WICPixelFormat128bppRGBAFixedPoint,  GUID_WICPixelFormat128bppRGBAFloat }, // DXGI_FORMAT_R32G32B32A32_FLOAT
	{ GUID_WICPixelFormat128bppRGBFixedPoint,   GUID_WICPixelFormat128bppRGBAFloat }, // DXGI_FORMAT_R32G32B32A32_FLOAT
	{ GUID_WICPixelFormat32bppRGBE,             GUID_WICPixelFormat128bppRGBAFloat }, // DXGI_FORMAT_R32G32B32A32_FLOAT

	{ GUID_WICPixelFormat32bppCMYK,             GUID_WICPixelFormat32bppRGBA }, // DXGI_FORMAT_R8G8B8A8_UNORM
	{ GUID_WICPixelFormat64bppCMYK,             GUID_WICPixelFormat64bppRGBA }, // DXGI_FORMAT_R16G16B16A16_UNORM
	{ GUID_WICPixelFormat40bppCMYKAlpha,        GUID_WICPixelFormat64bppRGBA }, // DXGI_FORMAT_R16G16B16A16_UNORM
	{ GUID_WICPixelFormat80bppCMYKAlpha,        GUID_WICPixelFormat64bppRGBA }, // DXGI_FORMAT_R16G16B16A16_UNORM

	{ GUID_WICPixelFormat32bppRGB,              GUID_WICPixelFormat32bppRGBA }, // DXGI_FORMAT_R8G8B8A8_UNORM
	{ GUID_WICPixelFormat64bppRGB,              GUID_WICPixelFormat64bppRGBA }, // DXGI_FORMAT_R16G16B16A16_UNORM
	{ GUID_WICPixelFormat64bppPRGBAHalf,        GUID_WICPixelFormat64bppRGBAHalf }, // DXGI_FORMAT_R16G16B16A16_FLOAT
};

bool GetTargetPixelFormat(const GUID* pSourceFormat, GUID* pTargetFormat)
{//查表确定兼容的最接近格式是哪个
	*pTargetFormat = *pSourceFormat;
	for (size_t i = 0; i < _countof(g_WICConvert); ++i)
	{
		if (InlineIsEqualGUID(g_WICConvert[i].source, *pSourceFormat))
		{
			*pTargetFormat = g_WICConvert[i].target;
			return true;
		}
	}
	return false;
}

DXGI_FORMAT GetDXGIFormatFromPixelFormat(const GUID* pPixelFormat)
{//查表确定最终对应的DXGI格式是哪一个
	for (size_t i = 0; i < _countof(g_WICFormats); ++i)
	{
		if (InlineIsEqualGUID(g_WICFormats[i].wic, *pPixelFormat))
		{
			return g_WICFormats[i].format;
		}
	}
	return DXGI_FORMAT_UNKNOWN;
}

ID3D12Resource *D3D12_CreateUploadBuffer(
	ID3D12Device  *device,
	uint32_t       size,
	const wchar_t *debug_name,
	const void    *initial_data,
	uint32_t       initial_data_size) 
{
	D3D12_HEAP_PROPERTIES heap_properties = {
		.Type = D3D12_HEAP_TYPE_UPLOAD,
	};

	D3D12_RESOURCE_DESC desc = {
		.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER,
		.Width            = size,
		.Height           = 1,
		.DepthOrArraySize = 1,
		.MipLevels        = 1,
		.Format           = DXGI_FORMAT_UNKNOWN,
		.SampleDesc       = { .Count = 1, .Quality = 0 },
		.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
	};

	ID3D12Resource *result;
	HRESULT hr = device->CreateCommittedResource(
		&heap_properties,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&result));

	ThrowIfFailed(hr);

	result->SetName(debug_name);

	if (initial_data)
	{
		assert(initial_data_size <= size || !"Your initial data is too big for this upload buffer!");

		void *mapped;

		D3D12_RANGE read_range = {};
		hr = result->Map(0, &read_range, &mapped);

		ThrowIfFailed(hr);

		memcpy(mapped, initial_data, initial_data_size);

		result->Unmap(0, nullptr);
	}

	return result;
}

static_assert(sizeof(D3D12_RootConstants) % 4 == 0, "Root constants have to be a multiple of 4 bytes");

const char g_shader_source[] = "#line " STRINGIFY(__LINE__) R"(

//------------------------------------------------------------------------
// Shader inputs

struct Vertex
{
	float2 position;
	float2 uv;
	float4 color;
};

struct PassConstants
{
	uint vbuffer_index;
};

struct RootConstants
{
	float2 offset;
	uint   texture_index;
};

ConstantBuffer<PassConstants> pass : register(b1);
ConstantBuffer<RootConstants> root : register(b0);

sampler s_nearest : register(s0);

//------------------------------------------------------------------------
// Vertex shader

void MainVS(
	in  uint   in_vertex_index  : SV_VertexID,
	out float4 out_position     : SV_Position,
	out float2 out_uv           : TEXCOORD,
	out float4 out_color        : COLOR)
{
    StructuredBuffer<Vertex> vbuffer = ResourceDescriptorHeap[pass.vbuffer_index];

	Vertex vertex = vbuffer.Load(in_vertex_index);

	out_position = float4(vertex.position + root.offset, 0, 1);
	out_uv       = vertex.uv;
	out_color    = vertex.color;
}

//------------------------------------------------------------------------
// Pixel shader

float4 MainPS(
	in float4 in_position : SV_Position,
	in float2 in_uv       : TEXCOORD,
	in float4 in_color    : COLOR) : SV_Target
{
	Texture2D texture = ResourceDescriptorHeap[root.texture_index];

	// float4 c = texture.Load(int3(0, 0, 0)); // 绕过 sampler
	// return c;

	float4 color = texture.SampleLevel(s_nearest, in_uv, 0);

	color *= in_color;

	return color;// + float4(root.texture_index * 0.1, 0, 0, 1);
}

)";

ID3D12PipelineState *D3D12_CreatePSO()
{
	IDxcBlob *error = nullptr;

	//------------------------------------------------------------------------
	// Compile vertex shader

	IDxcBlob *vs = nullptr;
	if (!DXC_CompileShader(g_shader_source, sizeof(g_shader_source), L"MainVS", L"vs_6_6", &vs, &error))
	{
		const char *error_message = (char *)error->GetBufferPointer();
		OutputDebugStringA("Failed to compile vertex shader:\n");
		OutputDebugStringA(error_message);
		assert(!"Failed to compile vertex shader, see debugger output for details");
	}
	COM_SAFE_RELEASE(error);

	//------------------------------------------------------------------------
	// Compile pixel shader

	IDxcBlob *ps = nullptr;
	if (!DXC_CompileShader(g_shader_source, sizeof(g_shader_source), L"MainPS", L"ps_6_6", &ps, &error))
	{
		const char *error_message = (char *)error->GetBufferPointer();
		OutputDebugStringA("Failed to compile pixel shader:\n");
		OutputDebugStringA(error_message);
		assert(!"Failed to compile pixel shader, see debugger output for details");
	}
	COM_SAFE_RELEASE(error);

	//------------------------------------------------------------------------
	// Create PSO

	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {
		.pRootSignature = g_d3d.rs_bindless,
		.VS = {
			.pShaderBytecode = vs->GetBufferPointer(),
			.BytecodeLength  = vs->GetBufferSize(),
		},
		.PS = {
			.pShaderBytecode = ps->GetBufferPointer(),
			.BytecodeLength  = ps->GetBufferSize(),
		},
		.BlendState = {
			.RenderTarget = {
				{
					.BlendEnable = true,
					.SrcBlend              = D3D12_BLEND_SRC_ALPHA,
					.DestBlend             = D3D12_BLEND_INV_SRC_ALPHA,
					.BlendOp               = D3D12_BLEND_OP_ADD,
					.SrcBlendAlpha         = D3D12_BLEND_INV_DEST_ALPHA,
					.DestBlendAlpha        = D3D12_BLEND_ONE,
					.BlendOpAlpha          = D3D12_BLEND_OP_ADD,
					.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL,
				},
			},
		},
		.SampleMask = D3D12_DEFAULT_SAMPLE_MASK,
		.RasterizerState = {
			.FillMode = D3D12_FILL_MODE_SOLID,
			.CullMode = D3D12_CULL_MODE_NONE,
			.FrontCounterClockwise = true,
		},
		.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
		.NumRenderTargets = 1,
		.RTVFormats = {
			DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
		},
		.SampleDesc = { .Count = 1, .Quality = 0 },
	};

	ID3D12PipelineState *pso;
	HRESULT hr = g_d3d.device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso));
	ThrowIfFailed(hr);

	return pso;
}

ID3D12Resource *D3D12_CreateTexture(
	ID3D12Device              *device,
	uint32_t                   width,
	uint32_t                   height,
	const wchar_t             *debug_name,
	const void                *initial_data = nullptr,
	ID3D12GraphicsCommandList *command_list = nullptr,
	D3D12_LinearAllocator     *allocator    = nullptr)
{
	D3D12_HEAP_PROPERTIES heap_properties = {
		.Type = D3D12_HEAP_TYPE_DEFAULT,
	};

	D3D12_RESOURCE_DESC desc = {
		.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
		.Width            = width,
		.Height           = height,
		.DepthOrArraySize = 1,
		.MipLevels        = 1,
		.Format           = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
		.SampleDesc       = { .Count = 1, .Quality = 0 },
		.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN,
	};

	ID3D12Resource *result;
	HRESULT hr = device->CreateCommittedResource(
		&heap_properties,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&result));

	ThrowIfFailed(hr);

	result->SetName(debug_name);

	if (initial_data)
	{
		assert(allocator    || !"If you want to provide the texture with initial data, we need an allocator with an upload heap");
		assert(command_list || !"If you want to provide the texture with initial data, we need a command list to issue a copy on");

		// Figure out the required layout of the texture
		uint64_t dst_size;
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT dst_layout;
		device->GetCopyableFootprints(&desc, 0, 1, 0, &dst_layout, nullptr, nullptr, &dst_size);

		// Create an upload heap allocation to serve as the copy source
		D3D12_BufferAllocation dst_alloc = allocator->Allocate((uint32_t)dst_size, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);

		size_t src_stride = sizeof(uint32_t)*width;
		size_t dst_stride = dst_layout.Footprint.RowPitch;

		// copy the texture data to the upload heap
		char *src = (char *)initial_data;
		char *dst = (char *)dst_alloc.cpu_base;

		for (size_t y = 0; y < height; y++)
		{
			memcpy(dst, src, sizeof(uint32_t)*width);

			src += src_stride;
			dst += dst_stride;
		}

		// issue the copy to the texture on the command list
		D3D12_TEXTURE_COPY_LOCATION src_loc = {
			.pResource = dst_alloc.buffer,
			.Type      = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
			.PlacedFootprint = {
				.Offset    = dst_alloc.offset,
				.Footprint = dst_layout.Footprint,
			},
		};

		D3D12_TEXTURE_COPY_LOCATION dst_loc = {
			.pResource        = result,
			.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
			.SubresourceIndex = 0,
		};

		command_list->CopyTextureRegion(&dst_loc, 0, 0, 0, &src_loc, nullptr);

		{
			D3D12_RESOURCE_BARRIER barrier = {
				.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
				.Transition = {
					.pResource   = result,
					.Subresource = 0,
					.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST,
					.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
				},
			};

			command_list->ResourceBarrier(1, &barrier);
		}
	}

	return result;
}

class RenderToTexture : public D3DApp
{
public:
	RenderToTexture(HINSTANCE hInstance);
	~RenderToTexture();

    virtual bool Initialize() override;

	void createDXCLibrary();
	void createMaterials();
	void createCommandAllocators();
    void createRootSignature();
	void createBindlessResources();
	void createBindlessRootSignature();
	void createSampler();
	void createHeaps();
	void createShaders();
	void createPipelineStateObjects();
    void createVertexBuffer();
    void createIndexBuffer();
	void createEarthVertexBuffer();
	void createEarthIndexBuffer();
	void createSkyboxVertexBuffer();
	void createQuadVertexBuffer();
	void createTexture();
	void createRenderTexture(RenderTexture& renderTexture);
    void createShaderResourceView();
	void createConstBufferView();
	void createQuadResources();
	void recordBundles();
	void setupMatrix();

	DXGI_FORMAT loadTexture2D(const WCHAR* pszTexcuteFileName, DXGI_FORMAT& stTextureFormat, ComPtr<ID3D12Resource>& pITexture, ComPtr<ID3D12CommandQueue>& pICommandQueue, ComPtr<ID3D12GraphicsCommandList>& pICommandList, ComPtr<ID3D12CommandAllocator>& pICommandAllocator, ComPtr<ID3D12PipelineState>& pIPipelineState, ComPtr<ID3D12Fence>& pIFence);
	void loadDDSTexture(const std::wstring& path, ComPtr<ID3D12Resource>& texture);
	
	void loadShader(const std::wstring& path, ComPtr<ID3DBlob>& vertexShader, ComPtr<ID3DBlob>& pixelShader);
	void loadShader(const std::wstring& path, ComPtr<IDxcBlob>& shaderCode, const std::wstring& entryPoint = L"VSMain", const std::wstring& targetProfile = L"vs_6_0");
	void loadShaderBinary(const std::string& shaderBaseName, ShaderData& shaderData);

	void createHeap(ComPtr<ID3D12Heap>& heap, uint64_t size, D3D12_HEAP_TYPE type = D3D12_HEAP_TYPE_UPLOAD, D3D12_HEAP_FLAGS flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS);
	void createDescriptorHeap(ComPtr<ID3D12DescriptorHeap>& descriptorHeap, uint32_t numDescriptors, D3D12_DESCRIPTOR_HEAP_TYPE type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAGS flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE);

	template<class T>
	void createVertexBuffer(const std::vector<T>& stTriangleVertices, ComPtr<ID3D12Resource>& vertexBuffer, D3D12_VERTEX_BUFFER_VIEW& VertexBufferView);
	uint32_t createIndexBuffer(const std::vector<uint32_t>& indices, ComPtr<ID3D12Resource>& indexBuffer, D3D12_INDEX_BUFFER_VIEW& indexBufferView);
	
	void createPipelineStateObject(const std::vector<D3D12_INPUT_ELEMENT_DESC>& inputElementDescs, const ComPtr<ID3D12RootSignature>& rootSignature, const ShaderData& shader, ComPtr<ID3D12PipelineState>& pipelineState, bool wireframe = false, bool depth = false, bool ccw = true, D3D12_PRIMITIVE_TOPOLOGY_TYPE primitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);

	void createActor(const GeometryGenerator::MeshData& meshData, uint32_t materialIndex, const float3& position = {0, 0, 0}, const float3& rotation = {0, 0, 0}, const float3& scale = {1, 1, 1});

	void resetUploadCommandList();
	void resetRenderCommandList();

	void executeUploadCommandList();

    void D3D12_InitScene(D3D12_Scene *scene);

	void D3D12_UpdateScene(D3D12_Scene *scene, double current_time);

	void DrawBindless();

	void processInput();

	void updateSceneBuffers();
	void beginCopy();
	void endCopy();
	
	private:
	virtual void OnResize()override;
	virtual void Update(const GameTimer& gt)override;
	virtual void Draw(const GameTimer& gt)override;

	DXGI_FORMAT	emRenderTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

	ComPtr<ID3DBlob> mISignatureBlob;
	ComPtr<ID3DBlob> mIErrorBlob;
	ComPtr<ID3D12RootSignature>	mRootSignature;
	ComPtr<ID3D12RootSignature>	mRootSignatureSkybox;

	// 编译Shader创建渲染管线状态对象
	ComPtr<ID3DBlob> mBlobVertexShader;
	ComPtr<ID3DBlob> mBlobPixelShader;

	ComPtr<ID3DBlob> mBlobVertexShaderSkybox;
	ComPtr<ID3DBlob> mBlobPixelShaderSkybox;

	ComPtr<ID3D12PipelineState>	mPipelineState;
	ComPtr<ID3D12PipelineState>	mPipelineStateSkybox;

	ComPtr<ID3D12Resource> mVertexBuffer;
	ComPtr<ID3D12Resource> mIndexBuffer;

	D3D12_VERTEX_BUFFER_VIEW mVertexBufferView = {};
	D3D12_INDEX_BUFFER_VIEW mIndexBufferView = {};

	ComPtr<ID3D12Resource> mVertexBufferSkybox;
	ComPtr<ID3D12Resource> mIndexBufferSkybox;
 
	D3D12_VERTEX_BUFFER_VIEW mVertexBufferViewSkybox = {};
	D3D12_INDEX_BUFFER_VIEW mIndexBufferViewSkybox = {};

	ComPtr<ID3D12Resource> mVertexBufferEarth;
	ComPtr<ID3D12Resource> mIndexBufferEarth;
 
	D3D12_VERTEX_BUFFER_VIEW mVertexBufferViewEarth = {};
	D3D12_INDEX_BUFFER_VIEW mIndexBufferViewEarth = {};


	float mAspectRatio = 3.0f;

	D3D12_RESOURCE_BARRIER stBeginResBarrier = {};
	D3D12_RESOURCE_BARRIER stEndResBarrier = {};

	ComPtr<ID3D12Heap> mTextureHeap;
	ComPtr<ID3D12Heap> mUploadHeap;
	ComPtr<ID3D12Resource> mCBVUpload;
	ComPtr<ID3D12Resource> mMaterialBufferUpload;
	ComPtr<ID3D12Resource> mCBVUploadSkybox;

	DXGI_FORMAT stTextureFormat = DXGI_FORMAT_UNKNOWN;

	ComPtr<ID3D12Resource> mTextureBear;
	ComPtr<ID3D12Resource> mTextureSkybox;
	ComPtr<ID3D12Resource> mTextureEarth;
	ComPtr<ID3D12Resource> mTextureJupiter;

	ComPtr<ID3D12DescriptorHeap> mSRVHeap;
	ComPtr<ID3D12DescriptorHeap> mSRVHeapSkybox;
	ComPtr<ID3D12DescriptorHeap> mSamplerHeap;

	uint32_t mSRVHeapIndex = 0;

	uint32_t nSRVDescriptorSize = 0U;
	uint32_t nSampleDescriptorSize = 0U;

	uint32_t mIndexCount;

	//初始的默认摄像机的位置
	float3 mEyePos = float3(0.0f, 0.0f, -20.0f); //眼睛位置
	float3 mLockAt = float3(0.0f, 0.0f, 0.0f);    //眼睛所盯的位置
	float3 mHeapUp = float3(0.0f, 1.0f, 0.0f);    //头部正上方位置
				
	float mYaw = 0.0f;				// 绕正Z轴的旋转量.
	float mPitch = 0.0f;			// 绕XZ平面的旋转量

	float mPalstance = 10.0f * glm::pi<float>() / 180.0f;	//物体旋转的角速度，单位：弧度/秒

	float mBoxSize = 6.0f;

	UINT64 mBufferOffset = 0;
	UINT64 mTextureHeapOffset = 0;
	const int BUFFER_SIZE = 1024;

	FrameMVPBuffer* mMVPBuffer = nullptr;
	FrameMVPBuffer* mMVPBufSkybox = nullptr;
	MaterialBuffer* mMaterialBuffer = nullptr;
	FrameMVOBuffer* mMVOBuffer = nullptr;

	uint32_t mQuadVertexCnt = 0;

	//常量缓冲区大小上对齐到256Bytes边界
	SIZE_T mMVPBufferSize = GRS_UPPER_DIV(sizeof(FrameMVPBuffer), 256) * 256;
	SIZE_T mMaterialBufferSize = GRS_UPPER_DIV(sizeof(MaterialBuffer), 256) * 256;
	SIZE_T mMVOBufferSize = GRS_UPPER_DIV(sizeof(FrameMVOBuffer), 256) * 256;

	float mSphereSize = 1.0f;

	//25、记录帧开始时间，和当前时间，以循环结束为界
	ULONGLONG mTimeFrameStart = ::GetTickCount64();
	ULONGLONG mTimeCurrent = mTimeFrameStart;

	//计算旋转角度需要的变量
	float dModelRotationYAngle = 0.0f;

	//球体的网格数据
	uint32_t mSphereIndexCnt = 0;
 
	//Sky Box的网格数据
	uint32_t mSkyboxIndexCnt = 4;
	std::vector<SkyboxVertex> mSkyboxVertices;

	ComPtr<ID3D12CommandAllocator>		mCommandAllocatorSkybox;
	ComPtr<ID3D12CommandAllocator>		mCommandAllocatorEarth;
	ComPtr<ID3D12CommandAllocator>		mCommandAllocatorCube;
	ComPtr<ID3D12CommandAllocator>		mCommandAllocatorQuad;
	ComPtr<ID3D12GraphicsCommandList>   mBundlesSkybox;
	ComPtr<ID3D12GraphicsCommandList>   mBundlesEarth;
	ComPtr<ID3D12GraphicsCommandList>   mBundlesCube;
	ComPtr<ID3D12GraphicsCommandList>   mBundlesQuad;
	ComPtr<ID3D12GraphicsCommandList>   mUploadCommandList;

	ComPtr<IDxcLibrary> mDXCLibrary;
	ComPtr<IDxcCompiler> mCompiler;
	ComPtr<IDxcUtils> mUtils;
    ComPtr<IDxcIncludeHandler> mIncludeHandler;
	ComPtr<IDxcBlob> mDxcVertexShader;
	ComPtr<IDxcBlob> mDxcPixelShader;
	ComPtr<IDxcBlob> mDxcVertexShaderSkybox;
	ComPtr<IDxcBlob> mDxcPixelShaderSkybox;
	ComPtr<IDxcBlob> mDxcVertexShaderQuad;
	ComPtr<IDxcBlob> mDxcPixelShaderQuad;

	ShaderData shader;
	ShaderData shaderSkybox;
	ShaderData shaderQuad;

	bool mSupportDXIL = true;

	DXGI_FORMAT	emRTFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	DXGI_FORMAT	emDSFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

	//被渲染纹理的清除颜色，创建时清除色和渲染前清除色保持一致，否则会出现一个未得到优化的警告
	float f4RTTexClearColor[4] = { 0.8f, 0.8f, 0.8f, 0.0f };

	ComPtr<ID3D12RootSignature>			mRSQuad;
	ComPtr<ID3D12PipelineState>			mPSOQuad;
	ComPtr<ID3D12DescriptorHeap>		mDHQuad;
	ComPtr<ID3D12DescriptorHeap>		mDHSampleQuad;
	ComPtr<ID3D12Resource>				mVBQuad;
	ComPtr<ID3D12Resource>			    mCBMVO;	//常量缓冲
	D3D12_VERTEX_BUFFER_VIEW			mVBViewQuad = {};

	float4x4 mxOrthographic;

	D3D12_RESOURCE_BARRIER stRTVStateTransBarrier = {};

	RenderTexture renderTexture;

	D3D12_LinearAllocator upload_arena;
	D3D12_LinearAllocator upload_arena_texture;

	D3D12_Scene scene;

	D3D12_DescriptorAllocator srvDescriptorAllocator;

	GeometryGenerator geometryGenerator;
	GeometryGenerator::MeshData box;
	GeometryGenerator::MeshData sphere;

	std::vector<MaterialBuffer> mMaterialBuffers;
	std::vector<SceneBuffer> mSceneBuffers;
	ComPtr<ID3D12Resource> objectBuffer;
	ComPtr<ID3D12Resource> uploadBuffer;

	CD3DX12_RESOURCE_BARRIER beginCopyBarrier;
	CD3DX12_RESOURCE_BARRIER endCopyBarrier;

	SceneBuffer* mappedSceneBuffer = nullptr;

	uint64_t sceneBufferSize;

    ComPtr<ID3D12CommandAllocator> mUploadCommandListAlloc;

	uint32_t meshID = 0;
	uint32_t materialIndex = 0;

	std::vector<Mesh> meshes;
	std::vector<Actor> actors;

	uint32_t actorIndex = 0;
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
	PSTR cmdLine, int showCmd)
{
	// Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	try
	{
		RenderToTexture theApp(hInstance);
		if (!theApp.Initialize())
			return 0;

		return theApp.Run();
	}
	catch (DxException& e)
	{
		MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
		return 0;
	}
}

void RenderToTexture::D3D12_InitScene(D3D12_Scene *scene)
{
	//------------------------------------------------------------------------
	// Create PSO

	scene->pso = D3D12_CreatePSO();

	//------------------------------------------------------------------------
	// Create index and vertex buffer

	uint16_t indices[] = {
		0, 1, 2,
	};

	float aspect_ratio = (float)g_d3d.window_w / (float)g_d3d.window_h;
	float triangle_width = 0.577f / aspect_ratio;

	Vertex2D vertices[] = {
		{ {            0.0f,  0.5f }, {  5.0f, 10.0f }, { 1, 1, 1, 1 } },
		{ {  triangle_width, -0.5f }, { 10.0f,  0.0f }, { 1, 1, 1, 1 } },
		{ { -triangle_width, -0.5f }, {  0.0f,  0.0f }, { 1, 1, 1, 1 } },
	};

	scene->ibuffer = D3D12_CreateUploadBuffer(g_d3d.device, sizeof(indices),  L"Index Buffer",  indices,  sizeof(indices));
	scene->vbuffer = D3D12_CreateUploadBuffer(g_d3d.device, sizeof(vertices), L"Vertex Buffer", vertices, sizeof(vertices));

	scene->vbuffer_srv = g_d3d.cbv_srv_uav.Allocate();

	{
		D3D12_SHADER_RESOURCE_VIEW_DESC desc = {
			.Format        = DXGI_FORMAT_UNKNOWN,
			.ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
			.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
			.Buffer = {
				.FirstElement        = 0,
				.NumElements         = 3,
				.StructureByteStride = sizeof(Vertex2D),
			},
		};

		g_d3d.device->CreateShaderResourceView(scene->vbuffer, &desc, scene->vbuffer_srv.cpu);
	}

	//------------------------------------------------------------------------
	// Make textures
	const uint32_t texture_pixels[][4*4] = {
		{ // checkerboard
			0xFF444444, 0xFF444444, 0xFFFFFFAA, 0xFFFFFFAA,
			0xFF444444, 0xFF444444, 0xFFFFFFAA, 0xFFFFFFAA,
			0xFFFFFFAA, 0xFFFFFFAA, 0xFF444444, 0xFF444444,
			0xFFFFFFAA, 0xFFFFFFAA, 0xFF444444, 0xFF444444,
		},
		{ // shifting checkerboard
			0xFF444444, 0xFF444444, 0xFFFFFFFF, 0xFFFFFFFF,
			0xFFFFFFFF, 0xFF444444, 0xFF444444, 0xFFFFFFFF,
			0xFFFFFFFF, 0xFFFFFFFF, 0xFF444444, 0xFF444444,
			0xFF444444, 0xFFFFFFFF, 0xFFFFFFFF, 0xFF444444,
		},
		{ // partytown
			0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0xFFFF00FF,
			0xFFFF00FF, 0xFF0000FF, 0xFF00FF00, 0xFFFF0000,
			0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0xFFFF00FF,
			0xFFFF00FF, 0xFF0000FF, 0xFF00FF00, 0xFFFF0000,
		},
		{ // rainbow stripes
			0xFFFF0000, 0xFFFF0000, 0xFFFF0000, 0xFFFF0000,
			0xFFFFFF00, 0xFFFFFF00, 0xFFFFFF00, 0xFFFFFF00,
			0xFF0000FF, 0xFF0000FF, 0xFF0000FF, 0xFF0000FF,
			0xFFFF00FF, 0xFFFF00FF, 0xFFFF00FF, 0xFFFF00FF,
		},
	};

	for (size_t i = 0; i < ArrayCount(texture_pixels); i++)
	{
		scene->textures     [i] = D3D12_CreateTexture(md3dDevice.Get(), 4, 4, L"Checkerboard", texture_pixels[i], mUploadCommandList.Get(), &upload_arena_texture);
		scene->textures_srvs[i] = g_d3d.cbv_srv_uav.Allocate();
		g_d3d.device->CreateShaderResourceView(scene->textures[i], nullptr, scene->textures_srvs[i].cpu);
	}

	//---------------------------------------------------------------------------------------------
	// 执行命令列表并等待纹理资源上传完成，这一步是必须的
	ThrowIfFailed(mUploadCommandList->Close());
	ID3D12CommandList* ppCommandLists[] = { mUploadCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	UINT64 FenceValue = 1;

	//---------------------------------------------------------------------------------------------
	// 18、创建一个Event同步对象，用于等待围栏事件通知
	HANDLE hFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (hFenceEvent == nullptr)
	{
		ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
	}

	//---------------------------------------------------------------------------------------------
	// 19、等待纹理资源正式复制完成先
	const UINT64 fence = FenceValue;
	ThrowIfFailed(mCommandQueue->Signal(mFence.Get(), fence));
	FenceValue++;

	//---------------------------------------------------------------------------------------------
	// 看命令有没有真正执行到围栏标记的这里，没有就利用事件去等待，注意使用的是命令队列对象的指针
	if (mFence->GetCompletedValue() < fence)
	{
		ThrowIfFailed(mFence->SetEventOnCompletion(fence, hFenceEvent));
		WaitForSingleObject(hFenceEvent, INFINITE);
	}

	//------------------------------------------------------------------------
	// Initialize triangle guys

	scene->triangle_guy_count = 4;

	for (size_t i = 0; i < scene->triangle_guy_count; i++)
	{
		scene->triangle_guys[i].texture = 3 - (uint32_t)i;
	}

	//------------------------------------------------------------------------

	scene->initialized = true;

	resetUploadCommandList();
}

void RenderToTexture::D3D12_UpdateScene(D3D12_Scene *scene, double current_time)
{
	for (size_t i = 0; i < scene->triangle_guy_count; i++)
	{
		TriangleGuy *guy = &scene->triangle_guys[i];
		
		guy->position.x = (float)(0.5 * sin(0.6 * (double)i + 1.25*current_time));
		guy->position.y = (float)(0.3 * sin(0.4 * (double)i + 0.65*current_time));
	}
}

RenderToTexture::RenderToTexture(HINSTANCE hInstance)
	: D3DApp(hInstance)
{
}

RenderToTexture::~RenderToTexture()
{
}

bool RenderToTexture::Initialize()
{
	if (!D3DApp::Initialize())
		return false;

	createDXCLibrary();
	createMaterials();
	createCommandAllocators();
	createRootSignature();
	createHeaps();
	createSampler();
	createShaders();
	createPipelineStateObjects();
	createVertexBuffer();
	createIndexBuffer();
	createEarthVertexBuffer();
	createEarthIndexBuffer();
	createSkyboxVertexBuffer();
	createTexture();
	createBindlessResources();
	createQuadVertexBuffer();
	createRenderTexture(renderTexture);
	createQuadResources();
	createShaderResourceView();
	createConstBufferView();
	recordBundles();
	setupMatrix();

    return true;
}

void RenderToTexture::loadShader(const std::wstring& path, ComPtr<ID3DBlob>& vertexShader, ComPtr<ID3DBlob>& pixelShader)
{
	#if defined(_DEBUG)
    // 调试状态下，打开Shader编译的调试标志，不优化
    uint32_t nCompileFlags =
        D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    uint32_t nCompileFlags = 0;
#endif

	//编译为行矩阵形式	   
	nCompileFlags |= D3DCOMPILE_PACK_MATRIX_ROW_MAJOR;

    ThrowIfFailed(D3DCompileFromFile(path.c_str(), nullptr, nullptr, "VSMain", "vs_5_1", nCompileFlags, 0, &vertexShader, nullptr));
    ThrowIfFailed(D3DCompileFromFile(path.c_str(), nullptr, nullptr, "PSMain", "ps_5_1", nCompileFlags, 0, &pixelShader, nullptr));
}

void RenderToTexture::loadShader(const std::wstring& path, ComPtr<IDxcBlob>& shaderCode, const std::wstring& entryPoint, const std::wstring& targetProfile)
{
	uint32_t codePage = CP_UTF8;

	ComPtr<IDxcBlobEncoding> sourceBlob;

	ThrowIfFailed(mDXCLibrary->CreateBlobFromFile(path.c_str(), &codePage, &sourceBlob));

	ComPtr<IDxcOperationResult> result;

	// 强制Row Major
	const wchar_t* arguments[] = {L"-Zpr"};

	HRESULT hr = mCompiler->Compile(
    sourceBlob.Get(), 				// pSource
    path.c_str(), 					// pSourceName
    entryPoint.c_str(), 			// pEntryPoint
    targetProfile.c_str(), 			// pTargetProfile
    arguments, 1, 					// pArguments, argCount
    NULL, 0, 						// pDefines, defineCount
    NULL, 							// pIncludeHandler
    &result); 						// ppResult

	if(SUCCEEDED(hr))
	{
		result->GetStatus(&hr);
	}

	if(FAILED(hr))
	{
		if(result)
		{
			ComPtr<IDxcBlobEncoding> errorsBlob;
			hr = result->GetErrorBuffer(&errorsBlob);
			if(SUCCEEDED(hr) && errorsBlob)
			{
				wprintf(L"Compilation failed with errors:\n%hs\n",
					(const char*)errorsBlob->GetBufferPointer());
			}
		}
		// Handle compilation error...
	}

	result->GetResult(&shaderCode);
}

void RenderToTexture::loadShaderBinary(const std::string& shaderBaseName, ShaderData& shaderData)
{	
	std::string vsName = shaderBaseName + "_VS.dxil";
	std::string psName = shaderBaseName + "_PS.dxil";

	std::ifstream fs(vsName, std::ios::binary | std::ios::ate);
    size_t size = (size_t)fs.tellg();
    shaderData.vsData.resize(size);
    fs.seekg(0);
    fs.read((char*)shaderData.vsData.data(), size);

	fs.close();

	fs.open(psName, std::ios::binary | std::ios::ate);

	size = (size_t)fs.tellg();
    shaderData.psData.resize(size);
    fs.seekg(0);
    fs.read((char*)shaderData.psData.data(), size);
}

void RenderToTexture::createHeap(ComPtr<ID3D12Heap>& heap, uint64_t size, D3D12_HEAP_TYPE type, D3D12_HEAP_FLAGS flags)
{
	D3D12_HEAP_DESC stUploadHeapDesc = {  };
	//尺寸依然是实际纹理数据大小的2倍并64K边界对齐大小
	stUploadHeapDesc.SizeInBytes = GRS_UPPER(size, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
	//注意上传堆肯定是Buffer类型，可以不指定对齐方式，其默认是64k边界对齐
	stUploadHeapDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
	stUploadHeapDesc.Properties.Type = type;		//上传堆类型
	stUploadHeapDesc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	stUploadHeapDesc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	//上传堆就是缓冲，可以摆放任意数据
	stUploadHeapDesc.Flags = flags;

	ThrowIfFailed(md3dDevice->CreateHeap(&stUploadHeapDesc, IID_PPV_ARGS(&heap)));
}

void RenderToTexture::createDescriptorHeap(ComPtr<ID3D12DescriptorHeap>& descriptorHeap, uint32_t numDescriptors, D3D12_DESCRIPTOR_HEAP_TYPE type, D3D12_DESCRIPTOR_HEAP_FLAGS flags) 
{
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.NumDescriptors = numDescriptors; // 假设我们有 3 个 sampler
	heapDesc.Type = type;
	heapDesc.Flags = flags; // 必须让着色器可见

	md3dDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&descriptorHeap));
}

template<class T>
void RenderToTexture::createVertexBuffer(const std::vector<T>& stTriangleVertices, ComPtr<ID3D12Resource>& vertexBuffer, D3D12_VERTEX_BUFFER_VIEW& VertexBufferView)
{
	size_t vertexBufferSize = stTriangleVertices.size() * sizeof(T);

	// mVertexCount = stTriangleVertices.size();

    D3D12_RESOURCE_DESC stResSesc = {};
    stResSesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    stResSesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    stResSesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    stResSesc.Format = DXGI_FORMAT_UNKNOWN;
    stResSesc.Width = vertexBufferSize;
    stResSesc.Height = 1;
    stResSesc.DepthOrArraySize = 1;
    stResSesc.MipLevels = 1;
    stResSesc.SampleDesc.Count = 1;
    stResSesc.SampleDesc.Quality = 0;

	//---------------------------------------------------------------------------------------------
	//使用定位方式在相同的上传堆上以“定位方式”创建顶点缓冲，注意第二个参数指出了堆中的偏移位置
	//按照堆边界对齐的要求，我们主动将偏移位置对齐到了64k的边界上
	ThrowIfFailed(md3dDevice->CreatePlacedResource(
		mUploadHeap.Get()
		, mBufferOffset
		, &stResSesc
		, D3D12_RESOURCE_STATE_GENERIC_READ
		, nullptr
		, IID_PPV_ARGS(&vertexBuffer)));

    UINT8 *pVertexDataBegin = nullptr;
    D3D12_RANGE stReadRange = { 0, 0 };

	ThrowIfFailed(vertexBuffer->Map(
		0
		, &stReadRange
		, reinterpret_cast<void**>(&pVertexDataBegin)));

	memcpy(pVertexDataBegin
		, stTriangleVertices.data()
		, vertexBufferSize);

	vertexBuffer->Unmap(0, nullptr);

	VertexBufferView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
	VertexBufferView.StrideInBytes = sizeof(T);
	VertexBufferView.SizeInBytes = (uint32_t)vertexBufferSize;

		//计算边界对齐的正确的偏移位置
	mBufferOffset = GRS_UPPER(mBufferOffset + vertexBufferSize, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
}

uint32_t RenderToTexture::createIndexBuffer(const std::vector<uint32_t>& indices, ComPtr<ID3D12Resource>& indexBuffer, D3D12_INDEX_BUFFER_VIEW& indexBufferView)
{
	const uint32_t nszIndexBuffer = (uint32_t)indices.size() * sizeof(uint32_t);

	CD3DX12_RANGE stReadRange(0, 0);		// We do not intend to read from this resource on the CPU.
	UINT8* pIndexDataBegin = nullptr;

	auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(nszIndexBuffer);

	ThrowIfFailed(md3dDevice->CreatePlacedResource(
			mUploadHeap.Get()
			, mBufferOffset
			, &bufferDesc
			, D3D12_RESOURCE_STATE_GENERIC_READ
			, nullptr
			, IID_PPV_ARGS(&indexBuffer)));

	ThrowIfFailed(indexBuffer->Map(0, &stReadRange, reinterpret_cast<void**>(&pIndexDataBegin)));
	memcpy(pIndexDataBegin, indices.data(), nszIndexBuffer);
	indexBuffer->Unmap(0, nullptr);

	indexBufferView.BufferLocation = indexBuffer->GetGPUVirtualAddress();
	indexBufferView.Format = DXGI_FORMAT_R32_UINT;
	indexBufferView.SizeInBytes = nszIndexBuffer;

	mBufferOffset = GRS_UPPER(mBufferOffset + nszIndexBuffer, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
	
	return (uint32_t)indices.size();
}

 void RenderToTexture::createPipelineStateObject(const std::vector<D3D12_INPUT_ELEMENT_DESC>& inputElementDescs, const ComPtr<ID3D12RootSignature>&	rootSignature, const ShaderData& shader, ComPtr<ID3D12PipelineState>& pipelineState, bool wireframe, bool depth, bool ccw, D3D12_PRIMITIVE_TOPOLOGY_TYPE primitiveTopologyType)
{
    // 定义渲染管线状态描述结构，创建渲染管线对象
    D3D12_GRAPHICS_PIPELINE_STATE_DESC stPSODesc = {};
    stPSODesc.InputLayout = {inputElementDescs.data(), (uint32_t)inputElementDescs.size()};

    stPSODesc.pRootSignature = rootSignature.Get();
	
	// if (mSupportDXIL)
	{
		stPSODesc.VS.pShaderBytecode = shader.vsData.data();
		stPSODesc.VS.BytecodeLength = shader.vsData.size();
		stPSODesc.PS.pShaderBytecode = shader.psData.data();
		stPSODesc.PS.BytecodeLength = shader.psData.size();
	}
	// else
	// {
		// stPSODesc.VS.pShaderBytecode = mBlobVertexShader->GetBufferPointer();
		// stPSODesc.VS.BytecodeLength = mBlobVertexShader->GetBufferSize();
		// stPSODesc.PS.pShaderBytecode = mBlobPixelShader->GetBufferPointer();
		// stPSODesc.PS.BytecodeLength = mBlobPixelShader->GetBufferSize();
	// }
	
	if (wireframe)
	{
   		stPSODesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
	}
	else
	{
    	stPSODesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	}

	if (ccw)
	{
		stPSODesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
	}
	else
	{
		stPSODesc.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT;
	}

    stPSODesc.BlendState.AlphaToCoverageEnable = FALSE;
    stPSODesc.BlendState.IndependentBlendEnable = FALSE;
    stPSODesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	if (depth)
	{
		stPSODesc.DepthStencilState.DepthEnable = TRUE;
    	stPSODesc.DepthStencilState.StencilEnable = TRUE;
	}
	else
	{
		stPSODesc.DepthStencilState.DepthEnable = FALSE;
		stPSODesc.DepthStencilState.StencilEnable = FALSE;
	}

    stPSODesc.PrimitiveTopologyType = primitiveTopologyType;

    stPSODesc.NumRenderTargets = 1;
    stPSODesc.RTVFormats[0] = emRenderTargetFormat;

    stPSODesc.SampleMask = UINT_MAX;
    stPSODesc.SampleDesc.Count = 1;

    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&stPSODesc, IID_PPV_ARGS(&pipelineState)));
}

void RenderToTexture::createActor(const GeometryGenerator::MeshData& meshData, uint32_t materialIndex, const float3& position, const float3& rotation, const float3& scale)
{
	Mesh mesh;

	auto simpleVertices = geometryGenerator.toSimpleVertices(meshData.vertices);

	createVertexBuffer(simpleVertices, mesh.vertexBuffer, mesh.vertexBufferView);
	mesh.indexCount = createIndexBuffer(meshData.indices32, mesh.indexBuffer, mesh.indexBufferView);
	
	meshes.emplace_back(mesh);

	Actor actor;

	actor.transform.position = position;
	actor.transform.rotation = rotation;
	actor.transform.scale = scale;

	actor.renderData.materialIndex = materialIndex;
	actor.renderData.meshID = (uint32_t)meshes.size() - 1;
	actor.renderData.objectIndex = actor.renderData.meshID;

	actors.emplace_back(actor);
}

void RenderToTexture::resetUploadCommandList()
{
	//---------------------------------------------------------------------------------------------
	//命令分配器先Reset一下，刚才已经执行过了一个复制纹理的命令
	ThrowIfFailed(mUploadCommandListAlloc->Reset());
	//Reset命令列表，并重新指定命令分配器和PSO对象
	ThrowIfFailed(mUploadCommandList->Reset(mUploadCommandListAlloc.Get(), nullptr));
	//---------------------------------------------------------------------------------------------
}

void RenderToTexture::executeUploadCommandList()
{
	//---------------------------------------------------------------------------------------------
	// 执行命令列表并等待纹理资源上传完成，这一步是必须的
	ThrowIfFailed(mUploadCommandList->Close());
	ID3D12CommandList* ppCommandLists[] = { mUploadCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
}

void RenderToTexture::resetRenderCommandList()
{
	//---------------------------------------------------------------------------------------------
	//命令分配器先Reset一下，刚才已经执行过了一个复制纹理的命令
	ThrowIfFailed(mDirectCmdListAlloc->Reset());
	//Reset命令列表，并重新指定命令分配器和PSO对象
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));
	//---------------------------------------------------------------------------------------------
}

void RenderToTexture::createDXCLibrary()
{
	ThrowIfFailed(DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(&mDXCLibrary)));

	ThrowIfFailed(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&mCompiler)));

	DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&mUtils));
    mUtils->CreateDefaultIncludeHandler(&mIncludeHandler);
}

void RenderToTexture::createMaterials()
{
	for (uint32_t i = 0; i < 3; i++)
	{
		mMaterialBuffers.emplace_back(MaterialBuffer{i, 0, i, 0});
	}
}

void RenderToTexture::createCommandAllocators()
{
	ThrowIfFailed(md3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_BUNDLE
		, IID_PPV_ARGS(&mCommandAllocatorEarth)));

	ThrowIfFailed(md3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_BUNDLE
		, mCommandAllocatorEarth.Get(), nullptr, IID_PPV_ARGS(&mBundlesEarth)));

	ThrowIfFailed(md3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_BUNDLE
		, IID_PPV_ARGS(&mCommandAllocatorCube)));

	ThrowIfFailed(md3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_BUNDLE
	, mCommandAllocatorCube.Get(), nullptr, IID_PPV_ARGS(&mBundlesCube)));

	ThrowIfFailed(md3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_BUNDLE
		, IID_PPV_ARGS(&mCommandAllocatorSkybox)));

	ThrowIfFailed(md3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_BUNDLE
		, mCommandAllocatorSkybox.Get(), nullptr, IID_PPV_ARGS(&mBundlesSkybox)));

	ThrowIfFailed(md3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_BUNDLE
		, IID_PPV_ARGS(&mCommandAllocatorQuad)));

	ThrowIfFailed(md3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_BUNDLE
		, mCommandAllocatorQuad.Get(), nullptr, IID_PPV_ARGS(&mBundlesQuad)));
		
	ThrowIfFailed(md3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT
		, IID_PPV_ARGS(&mUploadCommandListAlloc)));

	ThrowIfFailed(md3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT
	, mUploadCommandListAlloc.Get(), nullptr, IID_PPV_ARGS(&mUploadCommandList)));
}

void RenderToTexture::createRootSignature()
{
	D3D12_FEATURE_DATA_ROOT_SIGNATURE stFeatureData = {};
	// 检测是否支持V1.1版本的根签名
	stFeatureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
	if (FAILED(md3dDevice->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &stFeatureData, sizeof(stFeatureData))))
	{
		stFeatureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
	}

	CD3DX12_DESCRIPTOR_RANGE1 srvRange;

	srvRange.Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		2,      // NumDescriptors
		0,    	// BaseShaderRegister = t0
		0      	// RegisterSpace = space0
	);

	// 在GPU上执行SetGraphicsRootDescriptorTable后，我们不修改命令列表中的SRV，因此我们可以使用默认Rang行为:
	// D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE
	CD3DX12_ROOT_PARAMETER1 stRootParameters[2];
	stRootParameters[0].InitAsConstants(4, 0);
	stRootParameters[1].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_ALL);

	// Earth
	// 指定D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED标识允许直接用index
	// 索引CBV, SRV和UAV，也就是bindless(ResourceDescriptorHeap[index])
	// 同理指定D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED标识允许直接用index
	// 索引SamplerDescriptorHeap[index]
	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC stRootSignatureDesc;
	stRootSignatureDesc.Init_1_1(_countof(stRootParameters), stRootParameters
		, 0, nullptr
		, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | 
		  D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
		  D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED);

	ComPtr<ID3DBlob> pISignatureBlob;
	ComPtr<ID3DBlob> pIErrorBlob;
	ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&stRootSignatureDesc
		, stFeatureData.HighestVersion
		, &pISignatureBlob
		, &pIErrorBlob));

	ThrowIfFailed(md3dDevice->CreateRootSignature(0
		, pISignatureBlob->GetBufferPointer()
		, pISignatureBlob->GetBufferSize()
		, IID_PPV_ARGS(&mRootSignature)));

	d3dSetDebugName(mRootSignature.Get(), "mRootSignature");

	// Skybox
	// 在GPU上执行SetGraphicsRootDescriptorTable后，我们不修改命令列表中的SRV，因此我们可以使用默认Rang行为:
	// D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE
	CD3DX12_DESCRIPTOR_RANGE1 stDSPRangesSkybox[1];
	stDSPRangesSkybox[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);

	CD3DX12_ROOT_PARAMETER1 stRootParametersSkybox[2];
	stRootParametersSkybox[0].InitAsDescriptorTable(1, &stDSPRangesSkybox[0], D3D12_SHADER_VISIBILITY_ALL);	//CBV是所有Shader可见
	stRootParametersSkybox[1].InitAsConstants(4, 1);

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC stRootSignatureDescSkybox;
	stRootSignatureDescSkybox.Init_1_1(_countof(stRootParametersSkybox), stRootParametersSkybox
		, 0, nullptr
		, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | 
		  D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
		  D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED);

	ComPtr<ID3DBlob> pISignatureBlobSkybox;
	ComPtr<ID3DBlob> pIErrorBlobSkybox;
	ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&stRootSignatureDescSkybox
		, stFeatureData.HighestVersion
		, &pISignatureBlobSkybox
		, &pIErrorBlobSkybox));

	ThrowIfFailed(md3dDevice->CreateRootSignature(0
		, pISignatureBlobSkybox->GetBufferPointer()
		, pISignatureBlobSkybox->GetBufferSize()
		, IID_PPV_ARGS(&mRootSignatureSkybox)));


	// 创建渲染矩形用的管线的根签名和状态对象
	// 检测是否支持V1.1版本的根签名
	stFeatureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

	if (FAILED(md3dDevice->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &stFeatureData, sizeof(stFeatureData))))
	{// 1.0版 直接丢异常退出了
		ThrowIfFailed(E_NOTIMPL);
	}

	// 创建渲染矩形的根签名对象
	D3D12_DESCRIPTOR_RANGE1 stDSPRangesQuad[1];
	stDSPRangesQuad[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	stDSPRangesQuad[0].NumDescriptors = 1;
	stDSPRangesQuad[0].BaseShaderRegister = 0;
	stDSPRangesQuad[0].RegisterSpace = 0;
	stDSPRangesQuad[0].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
	stDSPRangesQuad[0].OffsetInDescriptorsFromTableStart = 0;

	D3D12_ROOT_PARAMETER1 stRootParametersQuad[1];
	stRootParametersQuad[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	stRootParametersQuad[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	stRootParametersQuad[0].DescriptorTable.NumDescriptorRanges = 1;
	stRootParametersQuad[0].DescriptorTable.pDescriptorRanges = &stDSPRangesQuad[0];

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC stRootSignatureDescQuad;

	stRootSignatureDescQuad.Init_1_1(_countof(stRootParametersQuad), stRootParametersQuad
		, 0, nullptr
		, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | 
		  D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
		  D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED);

	// stRootSignatureDescQuad.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
	// stRootSignatureDescQuad.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	// stRootSignatureDescQuad.Desc_1_1.NumParameters = _countof(stRootParametersQuad);
	// stRootSignatureDescQuad.Desc_1_1.pParameters = stRootParametersQuad;
	// stRootSignatureDescQuad.Desc_1_1.NumStaticSamplers = 0;
	// stRootSignatureDescQuad.Desc_1_1.pStaticSamplers = nullptr;

	ComPtr<ID3DBlob> pISignatureBlobQuad;
	ComPtr<ID3DBlob> pIErrorBlobQuad;

	ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&stRootSignatureDescQuad
		, stFeatureData.HighestVersion
		, &pISignatureBlobQuad
		, &pIErrorBlobQuad));

	// ThrowIfFailed(D3D12SerializeVersionedRootSignature(&stRootSignatureDescQuad
	// 	, &pISignatureBlobQuad
	// 	, &pIErrorBlobQuad));

	ThrowIfFailed(md3dDevice->CreateRootSignature(0
		, pISignatureBlobQuad->GetBufferPointer()
		, pISignatureBlobQuad->GetBufferSize()
		, IID_PPV_ARGS(&mRSQuad)));

	d3dSetDebugName(mRSQuad.Get(), "mRSQuad");
}

void RenderToTexture::createBindlessResources()
{
	upload_arena.Init(md3dDevice.Get(), (uint32_t)KiB(64));
	upload_arena_texture.Init(md3dDevice.Get(), (uint32_t)KiB(64));

	g_d3d.factory = mdxgiFactory.Get();
	g_d3d.device = md3dDevice.Get();
	g_d3d.window_w = mClientWidth;
	g_d3d.window_h = mClientHeight;
	g_d3d.fence = mFence.Get();
	g_d3d.queue = mCommandQueue.Get();
	g_d3d.allocator = mDirectCmdListAlloc.Get();

	g_d3d.cbv_srv_uav.Init(md3dDevice.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 6, true,  L"CBV SRV UAV Heap");

	srvDescriptorAllocator.Init(md3dDevice.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 5, true,  L"My CBV SRV UAV Heap");

	createBindlessRootSignature();

	DXC_Init();
	D3D12_InitScene(&scene);

	uint32_t objectCount = (uint32_t)actors.size();
	sceneBufferSize = sizeof(SceneBuffer) * objectCount;

	mSceneBuffers.resize(objectCount);

	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = sceneBufferSize;
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	desc.Flags = D3D12_RESOURCE_FLAG_NONE;

	auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

	md3dDevice->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		D3D12_RESOURCE_STATE_COMMON, // 后面会转
		nullptr,
		IID_PPV_ARGS(&objectBuffer));

		objectBuffer->SetName(L"objectBuffer");

	heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	
	md3dDevice->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&uploadBuffer));

		uploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mappedSceneBuffer));

	beginCopyBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
        objectBuffer.Get(),
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_COPY_DEST);

	endCopyBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
			objectBuffer.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void RenderToTexture::createBindlessRootSignature()
{
	D3D12_ROOT_PARAMETER1 parameters[D3D12_RootParameter_COUNT] = {};

	parameters[D3D12_RootParameter_32bit_constants] = {
		.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,
		.Constants = {
			.ShaderRegister = 0,
			.RegisterSpace  = 0,
			.Num32BitValues = 58, // 58 because each root cbv takes 2 uints, and the max is 64
		},
	};

	parameters[D3D12_RootParameter_pass_cbv] = {
		.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV,
		.Descriptor = {
			.ShaderRegister = 1,
			.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE,
		},
	};

	parameters[D3D12_RootParameter_view_cbv] = {
		.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV,
		.Descriptor = {
			.ShaderRegister = 2,
			.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE,
		},
	};

	parameters[D3D12_RootParameter_global_cbv] = {
		.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV,
		.Descriptor = {
			.ShaderRegister = 3,
			.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE,
		},
	};

	D3D12_STATIC_SAMPLER_DESC samplers[] = {
		{ // s_nearest
			.Filter           = D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR,
			.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
			.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
			.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
			.MipLODBias       = 0.0f,
			.MinLOD           = 0.0f,
			.MaxLOD           = D3D12_FLOAT32_MAX,
			.ShaderRegister   = 0,
			.RegisterSpace    = 0,
			.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL,
		},
	};

	D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc = {
		.Version = D3D_ROOT_SIGNATURE_VERSION_1_1,
		.Desc_1_1 = {
			.NumParameters     = ArrayCount(parameters),
			.pParameters       = parameters,
			.NumStaticSamplers = ArrayCount(samplers),
			.pStaticSamplers   = samplers,
			.Flags             = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED,
		},
	};

	ID3DBlob *serialized_desc = nullptr;

	auto hr = D3D12SerializeVersionedRootSignature(&desc, &serialized_desc, nullptr);
	ThrowIfFailed(hr);

	hr = g_d3d.device->CreateRootSignature(0, serialized_desc->GetBufferPointer(), serialized_desc->GetBufferSize(), IID_PPV_ARGS(&g_d3d.rs_bindless));
	ThrowIfFailed(hr);

	serialized_desc->Release();
}

void RenderToTexture::createSampler()
{
	D3D12_SAMPLER_DESC samplerDescs[3] = {};

	// 线性采样器
	samplerDescs[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	samplerDescs[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDescs[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDescs[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDescs[0].MinLOD = 0;
	samplerDescs[0].MaxLOD = D3D12_FLOAT32_MAX;

	// 最近点采样
	samplerDescs[1].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
	samplerDescs[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplerDescs[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplerDescs[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplerDescs[1].MinLOD = 0;
	samplerDescs[1].MaxLOD = D3D12_FLOAT32_MAX;

	//创建Skybox的采样器
	samplerDescs[2].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;

	samplerDescs[2].MinLOD = 0;
	samplerDescs[2].MaxLOD = D3D12_FLOAT32_MAX;
	samplerDescs[2].MipLODBias = 0.0f;
	samplerDescs[2].MaxAnisotropy = 1;
	samplerDescs[2].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	samplerDescs[2].BorderColor[0] = 0.0f;
	samplerDescs[2].BorderColor[1] = 0.0f;
	samplerDescs[2].BorderColor[2] = 0.0f;
	samplerDescs[2].BorderColor[3] = 0.0f;
	samplerDescs[2].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDescs[2].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDescs[2].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;

	auto samplerHandle = mSamplerHeap->GetCPUDescriptorHandleForHeapStart();

	for (int i = 0; i < 3; ++i)
	{
		md3dDevice->CreateSampler(&samplerDescs[i], samplerHandle);
		samplerHandle.ptr += nSampleDescriptorSize;
	}
}

void RenderToTexture::createHeaps()
{
	createHeap(mUploadHeap, BUFFER_SIZE * BUFFER_SIZE * 4 * 20);

	mUploadHeap->SetName(L"mUploadHeap");

	// D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES | D3D12_HEAP_FLAG_DENY_BUFFERS
	// 拒绝渲染目标纹理、拒绝深度蜡板纹理，实际就只是用来摆放普通纹理
	createHeap(mTextureHeap, GRS_UPPER(10 * BUFFER_SIZE * BUFFER_SIZE, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT), D3D12_HEAP_TYPE_DEFAULT, D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES | D3D12_HEAP_FLAG_DENY_BUFFERS);

	mTextureHeap->SetName(L"mTextureHeap");

	createDescriptorHeap(mSamplerHeap, 3, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);

	mSamplerHeap->SetName(L"mSamplerHeap");
	
	nSRVDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	nSampleDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
}

void RenderToTexture::createShaders()
{
	// loadShader(L"shader/textureCube.hlsl", mBlobVertexShader, mBlobPixelShader);
	// loadShader(L"shader/skybox.hlsl", mBlobVertexShaderSkybox, mBlobPixelShaderSkybox);

	// D3D12_FEATURE_DATA_D3D12_OPTIONS7 opt7{};
	// md3dDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &opt7, sizeof(opt7));

	// mSupportDXIL = opt7.MeshShaderTier != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED;

	// loadShader(L"shader/textureCubeDxc.hlsl", mDxcVertexShader, L"VSMain", L"vs_6_0");
	// loadShader(L"shader/textureCubeDxc.hlsl", mDxcPixelShader, L"PSMain", L"ps_6_0");

	// loadShader(L"shader/skybox.hlsl", mDxcVertexShaderSkybox, L"VSMain", L"vs_6_0");
	// loadShader(L"shader/skybox.hlsl", mDxcPixelShaderSkybox, L"PSMain", L"ps_6_0");

	// loadShader(L"shader/quad.hlsl", mDxcVertexShaderQuad, L"VSMain", L"vs_6_0");
	// loadShader(L"shader/quad.hlsl", mDxcPixelShaderQuad, L"PSMain", L"ps_6_0");

	loadShaderBinary("shader/textureCubeDxc", shader);
	loadShaderBinary("shader/skybox", shaderSkybox);
	loadShaderBinary("shader/quad", shaderQuad);
}

void RenderToTexture::createPipelineStateObjects()
{
    // Define the vertex input layout.
    std::vector<D3D12_INPUT_ELEMENT_DESC> stInputElementDescs =
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{"NORMAL", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

    // 定义渲染管线状态描述结构，创建渲染管线对象
	createPipelineStateObject(stInputElementDescs, mRootSignature, shader, mPipelineState);

	d3dSetDebugName(mPipelineState.Get(), "mPipelineState");

	// 天空盒子只有顶点只有位置参数
	std::vector<D3D12_INPUT_ELEMENT_DESC> stIALayoutSkybox =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	// 创建Skybox的(PSO)对象 注意天空盒子不需要深度测试
	createPipelineStateObject(stIALayoutSkybox, mRootSignatureSkybox, shaderSkybox, mPipelineStateSkybox);

	d3dSetDebugName(mPipelineStateSkybox.Get(), "mPipelineStateSkybox");

	std::vector<D3D12_INPUT_ELEMENT_DESC> stInputElementDescsQuad =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR",	  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,		 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	// 创建矩形的PSO对象
	createPipelineStateObject(stInputElementDescsQuad, mRSQuad, shaderQuad, mPSOQuad);

	d3dSetDebugName(mPSOQuad.Get(), "mPSOQuad");
}

void RenderToTexture::createVertexBuffer()
{
	box = geometryGenerator.createBox(mBoxSize, mBoxSize, mBoxSize, 0);

	auto boxVertices = geometryGenerator.toSimpleVertices(box.vertices);

	createVertexBuffer(boxVertices, mVertexBuffer, mVertexBufferView);

	createActor(box, 1, float3(0, 1, 0), float3(0, 0, 0), float3(2, 2, 2));
}

void RenderToTexture::createIndexBuffer()
{
	mIndexCount = createIndexBuffer(box.indices32, mIndexBuffer, mIndexBufferView);
}

void RenderToTexture::createEarthVertexBuffer()
{
	sphere = geometryGenerator.createSphere(1.0f, 16, 16);

	createVertexBuffer(sphere.vertices, mVertexBufferEarth, mVertexBufferViewEarth);

	createActor(sphere, 0, float3(0, 1, 0), float3(0, 0, 0), float3(2, 2, 2));
}

void RenderToTexture::createEarthIndexBuffer()
{
	mSphereIndexCnt = createIndexBuffer(sphere.indices32, mIndexBufferEarth, mIndexBufferViewEarth);
}

void RenderToTexture::createSkyboxVertexBuffer()
{
		//21、定义Sky的3D数据结构
	{
		float scale = 1.0f;
		float fHighW = -1.0f * scale - (1.0f / (float)mClientWidth);
		float fHighH = -1.0f * scale - (1.0f / (float)mClientHeight);
		float fLowW = 1.0f * scale + (1.0f / (float)mClientWidth);
		float fLowH = 1.0f * scale + (1.0f / (float)mClientHeight);

		mSkyboxVertices.resize(4);

		mSkyboxVertices[0].m_vPos = float4(fLowW, fLowH, 1.0f, 1.0f);
		mSkyboxVertices[1].m_vPos = float4(fLowW, fHighH, 1.0f, 1.0f);
		mSkyboxVertices[2].m_vPos = float4(fHighW, fLowH, 1.0f, 1.0f);
		mSkyboxVertices[3].m_vPos = float4(fHighW, fHighH, 1.0f, 1.0f);
	}

	createVertexBuffer(mSkyboxVertices, mVertexBufferSkybox, mVertexBufferViewSkybox);
}

void RenderToTexture::createQuadVertexBuffer()
{
	//矩形框的顶点结构变量
	//注意这里定义了一个单位大小的正方形，后面通过缩放和位移矩阵变换到合适的大小，这个技术常用于Spirit、UI等2D渲染
	std::vector<VertexQuad> stTriangleVertices =
	{
		// v0 - Left Top
		{ float4(-1.0f,  1.0f, 0.0f, 1.0f),
		  float4(1,1,1,1),
		  float2(0.0f, 0.0f) },

		// v1 - Left Bottom
		{ float4(-1.0f, -1.0f, 0.0f, 1.0f),
		  float4(1,1,1,1),
		  float2(0.0f, 1.0f) },

		// v2 - Right Top
		{ float4( 1.0f,  1.0f, 0.0f, 1.0f),
		  float4(1,1,1,1),
		  float2(1.0f, 0.0f) },

		// v3 - Right Bottom
		{ float4( 1.0f, -1.0f, 0.0f, 1.0f),
		  float4(1,1,1,1),
		  float2(1.0f, 1.0f) 
		},
	};

	createVertexBuffer(stTriangleVertices, mVBQuad, mVBViewQuad);

	mQuadVertexCnt = (uint32_t)stTriangleVertices.size();
}

void RenderToTexture::recordBundles()
{
	// Cube
	mBundlesCube->SetGraphicsRootSignature(mRootSignature.Get());
	mBundlesCube->SetPipelineState(mPipelineState.Get());

	ID3D12DescriptorHeap* ppHeaps[] = { mSRVHeap.Get(), mSamplerHeap.Get() };
	mBundlesCube->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

	MaterialBuffer buffer = 
	{
		.textureID = 0,
		.samplerID = 0
	};

	uint32_t count = sizeof(buffer) / sizeof(uint32_t);

	mBundlesCube->SetGraphicsRoot32BitConstants(0, count, &buffer, 0);

	CD3DX12_GPU_DESCRIPTOR_HANDLE stGPUCBVHandle(mSRVHeap->GetGPUDescriptorHandleForHeapStart()
	, 3
	, nSRVDescriptorSize);
	
	mBundlesCube->SetGraphicsRootDescriptorTable(1, stGPUCBVHandle);

	mBundlesCube->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	mBundlesCube->IASetVertexBuffers(0, 1, &mVertexBufferView);
	mBundlesCube->IASetIndexBuffer(&mIndexBufferView);

	mBundlesCube->DrawIndexedInstanced(mIndexCount, 1, 0, 0, 0);

	ThrowIfFailed(mBundlesCube->Close());

	// Earth
	mBundlesEarth->SetGraphicsRootSignature(mRootSignature.Get());
	mBundlesEarth->SetPipelineState(mPipelineState.Get());

	mBundlesEarth->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

	buffer = 
	{
		.textureID = 1,
		.samplerID = 0
	};

	mBundlesEarth->SetGraphicsRoot32BitConstants(0, count, &buffer, 0);
	
	mBundlesEarth->SetGraphicsRootDescriptorTable(1, stGPUCBVHandle);

	mBundlesEarth->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	mBundlesEarth->IASetVertexBuffers(0, 1, &mVertexBufferViewEarth);
	mBundlesEarth->IASetIndexBuffer(&mIndexBufferViewEarth);

	mBundlesEarth->DrawIndexedInstanced(mSphereIndexCnt, 1, 0, 0, 0);

	ThrowIfFailed(mBundlesEarth->Close());

	// Skybox
	//=================================================================================================
	//Skybox的捆绑包
	ID3D12DescriptorHeap* ppHeapsSkybox[] = { mSRVHeapSkybox.Get(), mSamplerHeap.Get() };
	mBundlesSkybox->SetDescriptorHeaps(_countof(ppHeapsSkybox), ppHeapsSkybox);

	mBundlesSkybox->SetPipelineState(mPipelineStateSkybox.Get());
	mBundlesSkybox->SetGraphicsRootSignature(mRootSignatureSkybox.Get());

	//设置SRV
	CD3DX12_GPU_DESCRIPTOR_HANDLE stGPUCBVHandleSkybox(mSRVHeapSkybox->GetGPUDescriptorHandleForHeapStart()
	, 1
	, nSRVDescriptorSize);

	mBundlesSkybox->SetGraphicsRootDescriptorTable(0, stGPUCBVHandleSkybox);

	buffer = 
	{
		.textureID = 0,
		.samplerID = 0
	};

	mBundlesSkybox->SetGraphicsRoot32BitConstants(1, count, &buffer, 0);

	mBundlesSkybox->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	mBundlesSkybox->IASetVertexBuffers(0, 1, &mVertexBufferViewSkybox);

	//Draw Call！！！
	mBundlesSkybox->DrawInstanced((uint32_t)mSkyboxVertices.size(), 1, 0, 0);
	ThrowIfFailed(mBundlesSkybox->Close());
	//=================================================================================================

	// 绘制矩形
	ID3D12DescriptorHeap* ppHeapsQuad[] = { mDHQuad.Get(), mDHSampleQuad.Get() };
	mBundlesQuad->SetDescriptorHeaps(_countof(ppHeapsQuad), ppHeapsQuad);
	
	mBundlesQuad->SetGraphicsRootSignature(mRSQuad.Get());

	mBundlesQuad->SetPipelineState(mPSOQuad.Get());

	D3D12_GPU_DESCRIPTOR_HANDLE stGPUCBVHandleQuad(mDHQuad->GetGPUDescriptorHandleForHeapStart());

	stGPUCBVHandleQuad.ptr += nSRVDescriptorSize;

	mBundlesQuad->SetGraphicsRootDescriptorTable(0, stGPUCBVHandleQuad);

	mBundlesQuad->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	mBundlesQuad->IASetVertexBuffers(0, 1, &mVBViewQuad);

	//Draw Call！！！
	mBundlesQuad->DrawInstanced(mQuadVertexCnt, 1, 0, 0);
	ThrowIfFailed(mBundlesQuad->Close());
}

void RenderToTexture::setupMatrix()
{
	// 1. 上下翻转（Y 取反）
	mxOrthographic = glm::scale(glm::mat4(1.0f), float3(1.0f, -1.0f, 1.0f));

	// 2. 微量偏移，矫正像素位置
	mxOrthographic = glm::translate(mxOrthographic, float3(-0.5f, +0.5f, 0.0f)) * mxOrthographic;

	// 3. 正交投影 OffCenter 版本（注意 GLM 的参数顺序）
	float left   = 0;
	float right  = (float)mClientWidth;
	float bottom = (float)mClientHeight;
	float top    = 0;
	float znear  = D3D12_MIN_DEPTH;
	float zfar   = D3D12_MAX_DEPTH;

	float4x4 ortho = glm::ortho(left, right, bottom, top, znear, zfar);

 	mxOrthographic = glm::translate(mxOrthographic, float3(160.0f, 120.0f, 0.0f));

	// 将正交矩阵乘到当前矩阵
	mxOrthographic = ortho * mxOrthographic;

	mxOrthographic = glm::scale(mxOrthographic, float3(160.0f, 120.0f, 1.0f));

	stRTVStateTransBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	stRTVStateTransBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	stRTVStateTransBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
}

DXGI_FORMAT RenderToTexture::loadTexture2D(const WCHAR* pszTexcuteFileName, DXGI_FORMAT& stTextureFormat, ComPtr<ID3D12Resource>& pITexture, ComPtr<ID3D12CommandQueue>& pICommandQueue, ComPtr<ID3D12GraphicsCommandList>& pICommandList, ComPtr<ID3D12CommandAllocator>& pICommandAllocator, ComPtr<ID3D12PipelineState>& pIPipelineState, ComPtr<ID3D12Fence>& pIFence)
{
	//---------------------------------------------------------------------------------------------
	// 16、使用WIC创建并加载一个2D纹理
	//使用纯COM方式创建WIC类厂对象，也是调用WIC第一步要做的事情
	ComPtr<IWICImagingFactory> pIWICFactory;
	ComPtr<IWICBitmapDecoder> pIWICDecoder;
	ComPtr<IWICBitmapFrameDecode> pIWICFrame;

	// 定义一个位图格式的图片数据对象接口
	ComPtr<IWICBitmapSource>pIBMP;

	ThrowIfFailed(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pIWICFactory)));

	//使用WIC类厂对象接口加载纹理图片，并得到一个WIC解码器对象接口，图片信息就在这个接口代表的对象中了
	ThrowIfFailed(pIWICFactory->CreateDecoderFromFilename(
		pszTexcuteFileName,              // 文件名
		NULL,                            // 不指定解码器，使用默认
		GENERIC_READ,                    // 访问权限
		WICDecodeMetadataCacheOnDemand,  // 若需要就缓冲数据 
		&pIWICDecoder                    // 解码器对象
	));

	// 获取第一帧图片(因为GIF等格式文件可能会有多帧图片，其他的格式一般只有一帧图片)
	// 实际解析出来的往往是位图格式数据
	ThrowIfFailed(pIWICDecoder->GetFrame(0, &pIWICFrame));

	WICPixelFormatGUID wpf = {};
	//获取WIC图片格式
	ThrowIfFailed(pIWICFrame->GetPixelFormat(&wpf));
	GUID tgFormat = {};

	//通过第一道转换之后获取DXGI的等价格式
	if (GetTargetPixelFormat(&wpf, &tgFormat))
	{
		stTextureFormat = GetDXGIFormatFromPixelFormat(&tgFormat);
	}

	if (DXGI_FORMAT_UNKNOWN == stTextureFormat)
	{// 不支持的图片格式 目前退出了事 
	 // 一般 在实际的引擎当中都会提供纹理格式转换工具，
	 // 图片都需要提前转换好，所以不会出现不支持的现象
		throw CGRSCOMException(S_FALSE);
	}

	if (!InlineIsEqualGUID(wpf, tgFormat))
	{// 这个判断很重要，如果原WIC格式不是直接能转换为DXGI格式的图片时
	 // 我们需要做的就是转换图片格式为能够直接对应DXGI格式的形式
		//创建图片格式转换器
		ComPtr<IWICFormatConverter> pIConverter;
		ThrowIfFailed(pIWICFactory->CreateFormatConverter(&pIConverter));

		//初始化一个图片转换器，实际也就是将图片数据进行了格式转换
		ThrowIfFailed(pIConverter->Initialize(
			pIWICFrame.Get(),                // 输入原图片数据
			tgFormat,						 // 指定待转换的目标格式
			WICBitmapDitherTypeNone,         // 指定位图是否有调色板，现代都是真彩位图，不用调色板，所以为None
			NULL,                            // 指定调色板指针
			0.f,                             // 指定Alpha阀值
			WICBitmapPaletteTypeCustom       // 调色板类型，实际没有使用，所以指定为Custom
		));
		// 调用QueryInterface方法获得对象的位图数据源接口
		ThrowIfFailed(pIConverter.As(&pIBMP));
	}
	else
	{
		//图片数据格式不需要转换，直接获取其位图数据源接口
		ThrowIfFailed(pIWICFrame.As(&pIBMP));
	}

	uint32_t nTextureW;
	uint32_t nTextureH;

	//获得图片大小（单位：像素）
	ThrowIfFailed(pIBMP->GetSize(&nTextureW, &nTextureH));

	//获取图片像素的位大小的BPP（Bits Per Pixel）信息，用以计算图片行数据的真实大小（单位：字节）
	ComPtr<IWICComponentInfo> pIWICmntinfo;
	ThrowIfFailed(pIWICFactory->CreateComponentInfo(tgFormat, pIWICmntinfo.GetAddressOf()));

	WICComponentType type;
	ThrowIfFailed(pIWICmntinfo->GetComponentType(&type));

	if (type != WICPixelFormat)
	{
		throw CGRSCOMException(S_FALSE);
	}

	ComPtr<IWICPixelFormatInfo> pIWICPixelinfo;
	ThrowIfFailed(pIWICmntinfo.As(&pIWICPixelinfo));

	uint32_t nBPP = 0;

	// 到这里终于可以得到BPP了，这也是我看的比较吐血的地方，为了BPP居然饶了这么多环节
	ThrowIfFailed(pIWICPixelinfo->GetBitsPerPixel(&nBPP));

	// 计算图片实际的行大小（单位：字节），这里使用了一个上取整除法即（A+B-1）/B ，
	// 这曾经被传说是微软的面试题,希望你已经对它了如指掌
	uint32_t nPicRowPitch = (uint32_t(nTextureW) * uint32_t(nBPP) + 7u) / 8u;

	D3D12_RESOURCE_DESC stTextureDesc = {};
	stTextureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	stTextureDesc.MipLevels = 1;
	stTextureDesc.Format = stTextureFormat; //DXGI_FORMAT_R8G8B8A8_UNORM;
	stTextureDesc.Width = nTextureW;
	stTextureDesc.Height = nTextureH;
	stTextureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	stTextureDesc.DepthOrArraySize = 1;
	stTextureDesc.SampleDesc.Count = 1;
	stTextureDesc.SampleDesc.Quality = 0;

	//-----------------------------------------------------------------------------------------------------------
	//使用“定位方式”来创建纹理，注意下面这个调用内部实际已经没有存储分配和释放的实际操作了，所以性能很高
	//同时可以在这个堆上反复调用CreatePlacedResource来创建不同的纹理，当然前提是它们不在被使用的时候，才考虑
	//重用堆
	ThrowIfFailed(md3dDevice->CreatePlacedResource(
		mTextureHeap.Get()
		, GRS_UPPER(mTextureHeapOffset, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT)
		, &stTextureDesc				//可以使用CD3DX12_RESOURCE_DESC::Tex2D来简化结构体的初始化
		, D3D12_RESOURCE_STATE_COPY_DEST
		, nullptr
		, IID_PPV_ARGS(&pITexture)));

	const UINT64 n64UploadBufferSize = GetRequiredIntermediateSize(pITexture.Get(), 0, 1);

	mTextureHeapOffset += GRS_UPPER(n64UploadBufferSize, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

	//ThrowIfFailed(pID3DDevice->CreatePlacedResource(pIUploadHeap.Get()
	//	, 0
	//	, &CD3DX12_RESOURCE_DESC::Buffer(n64UploadBufferSize)
	//	, D3D12_RESOURCE_STATE_GENERIC_READ
	//	, nullptr
	//	, IID_PPV_ARGS(&pITextureUpload)));

	//按照资源缓冲大小来分配实际图片数据存储的内存大小
	void* pbPicData = ::HeapAlloc(::GetProcessHeap(), HEAP_ZERO_MEMORY, (size_t)n64UploadBufferSize);
	if (nullptr == pbPicData)
	{
		throw CGRSCOMException(HRESULT_FROM_WIN32(GetLastError()));
	}

	//从图片中读取出数据
	ThrowIfFailed(pIBMP->CopyPixels(nullptr
		, nPicRowPitch
		, static_cast<uint32_t>(nPicRowPitch * nTextureH)   //注意这里才是图片数据真实的大小，这个值通常小于缓冲的大小
		, reinterpret_cast<BYTE*>(pbPicData)));

	//获取向上传堆拷贝纹理数据的一些纹理转换尺寸信息
	//对于复杂的DDS纹理这是非常必要的过程
	UINT64 n64RequiredSize = 0u;
	uint32_t   nNumSubresources = 1u;  //我们只有一副图片，即子资源个数为1
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT stTxtLayouts = {};
	UINT64 n64TextureRowSizes = 0u;
	uint32_t   nTextureRowNum = 0u;

	D3D12_RESOURCE_DESC stDestDesc = pITexture->GetDesc();

	ComPtr<ID3D12Resource> textureUpload;

	auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(BUFFER_SIZE * BUFFER_SIZE * 4);

	ThrowIfFailed(md3dDevice->CreatePlacedResource(mUploadHeap.Get()
	, mBufferOffset
	, &bufferDesc
	, D3D12_RESOURCE_STATE_GENERIC_READ
	, nullptr
	, IID_PPV_ARGS(&textureUpload)));

	md3dDevice->GetCopyableFootprints(&stDestDesc
		, 0
		, nNumSubresources
		, 0
		, &stTxtLayouts
		, &nTextureRowNum
		, &n64TextureRowSizes
		, &n64RequiredSize);

	//因为上传堆实际就是CPU传递数据到GPU的中介
	//所以我们可以使用熟悉的Map方法将它先映射到CPU内存地址中
	//然后我们按行将数据复制到上传堆中
	//需要注意的是之所以按行拷贝是因为GPU资源的行大小
	//与实际图片的行大小是有差异的,二者的内存边界对齐要求是不一样的
	BYTE* pData = nullptr;
	ThrowIfFailed(textureUpload->Map(0, NULL, reinterpret_cast<void**>(&pData)));

	BYTE* pDestSlice = reinterpret_cast<BYTE*>(pData) + stTxtLayouts.Offset;
	const BYTE* pSrcSlice = reinterpret_cast<const BYTE*>(pbPicData);
	for (uint32_t y = 0; y < nTextureRowNum; ++y)
	{
		memcpy(pDestSlice + static_cast<SIZE_T>(stTxtLayouts.Footprint.RowPitch) * y
			, pSrcSlice + static_cast<SIZE_T>(nPicRowPitch) * y
			, nPicRowPitch);
	}
	//取消映射 对于易变的数据如每帧的变换矩阵等数据，可以撒懒不用Unmap了，
	//让它常驻内存,以提高整体性能，因为每次Map和Unmap是很耗时的操作
	//因为现在起码都是64位系统和应用了，地址空间是足够的，被长期占用不会影响什么
	textureUpload->Unmap(0, NULL);

	//释放图片数据，做一个干净的程序员
	::HeapFree(::GetProcessHeap(), 0, pbPicData);

	//向命令队列发出从上传堆复制纹理数据到默认堆的命令
	CD3DX12_TEXTURE_COPY_LOCATION Dst1(pITexture.Get(), 0);
	CD3DX12_TEXTURE_COPY_LOCATION Src1(textureUpload.Get(), stTxtLayouts);
	pICommandList->CopyTextureRegion(&Dst1, 0, 0, 0, &Src1, nullptr);

	//设置一个资源屏障，同步并确认复制操作完成
	//直接使用结构体然后调用的形式
	D3D12_RESOURCE_BARRIER stResBar = {};
	stResBar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	stResBar.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	stResBar.Transition.pResource = pITexture.Get();
	stResBar.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	stResBar.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	stResBar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

	pICommandList->ResourceBarrier(1, &stResBar);

	//---------------------------------------------------------------------------------------------
	// 执行命令列表并等待纹理资源上传完成，这一步是必须的
	ThrowIfFailed(pICommandList->Close());
	ID3D12CommandList* ppCommandLists[] = { pICommandList.Get() };
	pICommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	UINT64 FenceValue = 1;

	//---------------------------------------------------------------------------------------------
	// 18、创建一个Event同步对象，用于等待围栏事件通知
	HANDLE hFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (hFenceEvent == nullptr)
	{
		ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
	}

	//---------------------------------------------------------------------------------------------
	// 19、等待纹理资源正式复制完成先
	const UINT64 fence = FenceValue;
	ThrowIfFailed(pICommandQueue->Signal(pIFence.Get(), fence));
	FenceValue++;

	//---------------------------------------------------------------------------------------------
	// 看命令有没有真正执行到围栏标记的这里，没有就利用事件去等待，注意使用的是命令队列对象的指针
	if (pIFence->GetCompletedValue() < fence)
	{
		ThrowIfFailed(pIFence->SetEventOnCompletion(fence, hFenceEvent));
		WaitForSingleObject(hFenceEvent, INFINITE);
	}

	resetUploadCommandList();

	return stTextureFormat;
}

void RenderToTexture::loadDDSTexture(const std::wstring& path, ComPtr<ID3D12Resource>& texture)
{
	//12、使用DDSLoader辅助函数加载Skybox的纹理
	TCHAR pszSkyboxTextureFile[] = _T("Texture/Sky_cube_1024.dds");

	//HRESULT DirectX::LoadDDSTextureFromFile(
	//	ID3D12Device* d3dDevice,
	//	const wchar_t* fileName,
	//	ID3D12Resource** texture,
	//	std::unique_ptr<uint8_t[]>& ddsData,
	//	std::vector<D3D12_SUBRESOURCE_DATA>& subresources,
	//	size_t maxsize,
	//	DDS_ALPHA_MODE* alphaMode,
	//	bool* isCubeMap);

	//======================================================================================================
	//加载Skybox的Cube Map需要的变量
	std::unique_ptr<uint8_t[]> ddsData;
	std::vector<D3D12_SUBRESOURCE_DATA> subresources;
	DDS_ALPHA_MODE emAlphaMode = DDS_ALPHA_MODE_UNKNOWN;
	bool bIsCube = false;
	//======================================================================================================

	ID3D12Resource* pIResSkyBox = nullptr;
	ThrowIfFailed(LoadDDSTextureFromFile(
		md3dDevice.Get()
		, path.c_str()
		, &pIResSkyBox
		, ddsData
		, subresources
		, SIZE_MAX
		, &emAlphaMode
		, &bIsCube));

	//上面函数加载的纹理在隐式默认堆上，两个Copy动作需要我们自己完成
	texture.Attach(pIResSkyBox);

	ComPtr<ID3D12Resource> textureUploadSkybox;

	uint64_t uploadBufferSkyboxSize = GetRequiredIntermediateSize(texture.Get(), 0, static_cast<uint32_t>(subresources.size()));

	auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSkyboxSize);
	ThrowIfFailed(md3dDevice->CreatePlacedResource(mUploadHeap.Get()
	, mBufferOffset
	, &bufferDesc
	, D3D12_RESOURCE_STATE_GENERIC_READ
	, nullptr
	, IID_PPV_ARGS(&textureUploadSkybox)));

	mBufferOffset = GRS_UPPER(mBufferOffset + uploadBufferSkyboxSize, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

	//18、上传Skybox的纹理
	{
		auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(texture.Get()
				, D3D12_RESOURCE_STATE_COPY_DEST
				, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

		UpdateSubresources(mUploadCommandList.Get()
			, texture.Get()
			, textureUploadSkybox.Get()
			, 0
			, 0
			, static_cast<uint32_t>(subresources.size())
			, subresources.data());

		mUploadCommandList->ResourceBarrier(1
			, &barrier);
	}

		//20、执行第二个Copy命令并确定所有的纹理都上传到了默认堆中
	{
		//---------------------------------------------------------------------------------------------
		// 执行命令列表并等待纹理资源上传完成，这一步是必须的
		ThrowIfFailed(mUploadCommandList->Close());
		ID3D12CommandList* ppCommandLists[] = { mUploadCommandList.Get() };
		mCommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

		UINT64 FenceValue = 1;

		//---------------------------------------------------------------------------------------------
		// 18、创建一个Event同步对象，用于等待围栏事件通知
		HANDLE hFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

		if (hFenceEvent == nullptr)
		{
			ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
		}

		//---------------------------------------------------------------------------------------------
		// 19、等待纹理资源正式复制完成先
		const UINT64 fence = FenceValue;
		ThrowIfFailed(mCommandQueue->Signal(mFence.Get(), fence));
		FenceValue++;

		//---------------------------------------------------------------------------------------------
		// 看命令有没有真正执行到围栏标记的这里，没有就利用事件去等待，注意使用的是命令队列对象的指针
		if (mFence->GetCompletedValue() < fence)
		{
			ThrowIfFailed(mFence->SetEventOnCompletion(fence, hFenceEvent));
			WaitForSingleObject(hFenceEvent, INFINITE);
		}
	}

	resetUploadCommandList();
}

void RenderToTexture::createTexture()
{
	loadTexture2D(_T("Texture/bear_flipX.jpg"), stTextureFormat, mTextureBear, mCommandQueue, mUploadCommandList, mDirectCmdListAlloc, mPipelineState, mFence);
	loadTexture2D(_T("Texture/金星.jpg"), stTextureFormat, mTextureEarth, mCommandQueue, mUploadCommandList, mDirectCmdListAlloc, mPipelineState, mFence);
	loadTexture2D(_T("Texture/木星.jpg"), stTextureFormat, mTextureJupiter, mCommandQueue, mUploadCommandList, mDirectCmdListAlloc, mPipelineState, mFence);

	d3dSetDebugName(mTextureBear.Get(), "mTextureBear");
	d3dSetDebugName(mTextureEarth.Get(), "mTextureEarth");
	d3dSetDebugName(mTextureJupiter.Get(), "mTextureJupiter");

	loadDDSTexture(L"Texture/Sky_cube_1024.dds", mTextureSkybox);

	d3dSetDebugName(mTextureSkybox.Get(), "mTextureSkybox");
}

void RenderToTexture::createRenderTexture(RenderTexture& renderTexture)
{
	D3D12_RESOURCE_DESC stRenderTargetResDesc = {};
	stRenderTargetResDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	stRenderTargetResDesc.Alignment = 0;
	stRenderTargetResDesc.Width = mClientWidth;
	stRenderTargetResDesc.Height = mClientHeight;
	stRenderTargetResDesc.DepthOrArraySize = 1;
	stRenderTargetResDesc.MipLevels = 1;
	stRenderTargetResDesc.Format = emRTFormat;
	stRenderTargetResDesc.SampleDesc.Count = 1;
	stRenderTargetResDesc.SampleDesc.Quality = 0;
	stRenderTargetResDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	stRenderTargetResDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	D3D12_CLEAR_VALUE stClear = {};
	stClear.Format = emRTFormat;
	memcpy(&stClear.Color, &f4RTTexClearColor, 4 * sizeof(float));

	D3D12_HEAP_PROPERTIES stDefautHeapProps = {};
	stDefautHeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
	stDefautHeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	stDefautHeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	stDefautHeapProps.CreationNodeMask = 1;
	stDefautHeapProps.VisibleNodeMask = 1;

	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&stDefautHeapProps
		, D3D12_HEAP_FLAG_NONE
		, &stRenderTargetResDesc
		// , D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
		// , D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
		, D3D12_RESOURCE_STATE_COPY_DEST
		, &stClear
		, IID_PPV_ARGS(&renderTexture.renderTarget)));

	d3dSetDebugName(renderTexture.renderTarget.Get(), "renderTexture.renderTarget");

	//创建RTV(渲染目标视图)描述符堆
	createDescriptorHeap(renderTexture.DHRTVTexture, 1, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	d3dSetDebugName(renderTexture.DHRTVTexture.Get(), "renderTexture.dhRTVTexture");

	md3dDevice->CreateRenderTargetView(renderTexture.renderTarget.Get(), nullptr, renderTexture.DHRTVTexture->GetCPUDescriptorHandleForHeapStart());

	D3D12_CLEAR_VALUE stDepthOptimizedClearValue = {};
	stDepthOptimizedClearValue.Format = emDSFormat;
	stDepthOptimizedClearValue.DepthStencil.Depth = 1.0f;
	stDepthOptimizedClearValue.DepthStencil.Stencil = 0;

	D3D12_RESOURCE_DESC stDSResDesc = {};
	stDSResDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	stDSResDesc.Alignment = 0;
	stDSResDesc.Width = mClientWidth;
	stDSResDesc.Height = mClientHeight;
	stDSResDesc.DepthOrArraySize = 1;
	stDSResDesc.MipLevels = 1;
	stDSResDesc.Format = emDSFormat;
	stDSResDesc.SampleDesc.Count = 1;
	stDSResDesc.SampleDesc.Quality = 0;
	stDSResDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	stDSResDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	//使用隐式默认堆创建一个深度蜡板缓冲区，
	//因为基本上深度缓冲区会一直被使用，重用的意义不大，所以直接使用隐式堆，图方便
	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&stDefautHeapProps
		, D3D12_HEAP_FLAG_NONE
		, &stDSResDesc
		, D3D12_RESOURCE_STATE_DEPTH_WRITE
		, &stDepthOptimizedClearValue
		, IID_PPV_ARGS(&renderTexture.depthStencilTexture)
	));

	d3dSetDebugName(renderTexture.depthStencilTexture.Get(), "renderTexture.depthStencilTexture");

	D3D12_DEPTH_STENCIL_VIEW_DESC stDepthStencilDesc = {};
	stDepthStencilDesc.Format = emDSFormat;
	stDepthStencilDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	stDepthStencilDesc.Flags = D3D12_DSV_FLAG_NONE;

	createDescriptorHeap(renderTexture.DHDSVTexture, 1, D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	
	d3dSetDebugName(renderTexture.DHDSVTexture.Get(), "renderTexture.DHDSVTexture");

	md3dDevice->CreateDepthStencilView(renderTexture.depthStencilTexture.Get()
		, &stDepthStencilDesc
		, renderTexture.DHDSVTexture->GetCPUDescriptorHandleForHeapStart());
}

void RenderToTexture::createShaderResourceView()
{
	// Earth
	// 3 SRV + 2 CBV + 1 StructuredBuffer
	createDescriptorHeap(mSRVHeap, 6, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);

	mSRVHeap->SetName(L"mSRVHeap");
	
	// Skybox
	// 1 SRV + 1 CBV
	createDescriptorHeap(mSRVHeapSkybox, 2, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);

	mSRVHeapSkybox->SetName(L"mSRVHeapSkybox");

    //---------------------------------------------------------------------------------------------
    // 最终创建SRV描述符
    D3D12_SHADER_RESOURCE_VIEW_DESC stSRVDesc = {};
    stSRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    stSRVDesc.Format = stTextureFormat;
    stSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    stSRVDesc.Texture2D.MipLevels = 1;

    CD3DX12_CPU_DESCRIPTOR_HANDLE stSRVHandle(mSRVHeap->GetCPUDescriptorHandleForHeapStart());

	assert(mTextureEarth != nullptr);

    md3dDevice->CreateShaderResourceView(mTextureEarth.Get(), &stSRVDesc, stSRVHandle);

    stSRVHandle.Offset(1, nSRVDescriptorSize);
	mSRVHeapIndex++;

	assert(mTextureJupiter != nullptr);

    md3dDevice->CreateShaderResourceView(mTextureJupiter.Get(), &stSRVDesc, stSRVHandle);

	stSRVHandle.Offset(1, nSRVDescriptorSize);
	mSRVHeapIndex++;

	assert(mTextureBear != nullptr);

    md3dDevice->CreateShaderResourceView(mTextureBear.Get(), &stSRVDesc, stSRVHandle);

	stSRVHandle.Offset(1, nSRVDescriptorSize);
	mSRVHeapIndex++;

	D3D12_RESOURCE_DESC stDescSkybox = mTextureSkybox->GetDesc();
	stSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
	stSRVDesc.Format = stDescSkybox.Format;
	stSRVDesc.TextureCube.MipLevels = stDescSkybox.MipLevels;

	assert(mTextureSkybox != nullptr);

	md3dDevice->CreateShaderResourceView(mTextureSkybox.Get(), &stSRVDesc, mSRVHeapSkybox->GetCPUDescriptorHandleForHeapStart());

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;   // StructuredBuffer 必须 UNKNOWN
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.NumElements = (uint32_t)actors.size();
	srvDesc.Buffer.StructureByteStride = sizeof(SceneBuffer);
	srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

	static_assert(sizeof(SceneBuffer) == 64);

	md3dDevice->CreateShaderResourceView(
		objectBuffer.Get(),
		&srvDesc,
		stSRVHandle
	);

	stSRVHandle.Offset(1, nSRVDescriptorSize);
	mSRVHeapIndex++;
}

void  RenderToTexture::createConstBufferView()
{
	// 创建常量缓冲 注意缓冲尺寸设置为256边界对齐大小
	auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(mMVPBufferSize);
	ThrowIfFailed(md3dDevice->CreatePlacedResource(
		mUploadHeap.Get()
		, mBufferOffset
		, &bufferDesc
		, D3D12_RESOURCE_STATE_GENERIC_READ
		, nullptr
		, IID_PPV_ARGS(&mCBVUpload)));

	// Map 之后就不再Unmap了 直接复制数据进去 这样每帧都不用map-copy-unmap浪费时间了
	ThrowIfFailed(mCBVUpload->Map(0, nullptr, reinterpret_cast<void**>(&mMVPBuffer)));

	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
	cbvDesc.BufferLocation = mCBVUpload->GetGPUVirtualAddress();
	cbvDesc.SizeInBytes = static_cast<uint32_t>(mMVPBufferSize);

	CD3DX12_CPU_DESCRIPTOR_HANDLE cbvSrvHandle(mSRVHeap->GetCPUDescriptorHandleForHeapStart()
		, mSRVHeapIndex
		, nSRVDescriptorSize);

	md3dDevice->CreateConstantBufferView(&cbvDesc, cbvSrvHandle);

	mBufferOffset = GRS_UPPER(mBufferOffset + mMVPBufferSize * sizeof(SkyboxVertex), D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

	// // 材质参数缓冲区
	// ThrowIfFailed(md3dDevice->CreatePlacedResource(
	// 	mUploadHeap.Get()
	// 	, mBufferOffset
	// 	, &CD3DX12_RESOURCE_DESC::Buffer(mMaterialBufferSize)
	// 	, D3D12_RESOURCE_STATE_GENERIC_READ
	// 	, nullptr
	// 	, IID_PPV_ARGS(&mMaterialBufferUpload)));

	// // Map 之后就不再Unmap了 直接复制数据进去 这样每帧都不用map-copy-unmap浪费时间了
	// ThrowIfFailed(mMaterialBufferUpload->Map(0, nullptr, reinterpret_cast<void**>(&mMaterialBuffer)));

	// cbvDesc.BufferLocation = mMaterialBufferUpload->GetGPUVirtualAddress();
	// cbvDesc.SizeInBytes = static_cast<uint32_t>(mMaterialBufferSize);

	// cbvSrvHandle.Offset(1, md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));

	// md3dDevice->CreateConstantBufferView(&cbvDesc, cbvSrvHandle);

	// mBufferOffset = GRS_UPPER(mBufferOffset + mMaterialBufferSize * sizeof(SkyboxVertex), D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

	// 创建常量缓冲 注意缓冲尺寸设置为256边界对齐大小
	bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(mMVPBufferSize);

	ThrowIfFailed(md3dDevice->CreatePlacedResource(
		mUploadHeap.Get()
		, mBufferOffset
		, &bufferDesc
		, D3D12_RESOURCE_STATE_GENERIC_READ
		, nullptr
		, IID_PPV_ARGS(&mCBVUploadSkybox)));

	// Map 之后就不再Unmap了 直接复制数据进去 这样每帧都不用map-copy-unmap浪费时间了
	ThrowIfFailed(mCBVUploadSkybox->Map(0, nullptr, reinterpret_cast<void**>(&mMVPBufSkybox)));

	cbvDesc.BufferLocation = mCBVUploadSkybox->GetGPUVirtualAddress();
	cbvDesc.SizeInBytes = static_cast<uint32_t>(mMVPBufferSize);

	CD3DX12_CPU_DESCRIPTOR_HANDLE cbvSrvHandleSkybox(mSRVHeapSkybox->GetCPUDescriptorHandleForHeapStart()
		, 1
		, nSRVDescriptorSize);

	md3dDevice->CreateConstantBufferView(&cbvDesc, cbvSrvHandleSkybox);

	mBufferOffset = GRS_UPPER(mBufferOffset + mMVPBufferSize * sizeof(SkyboxVertex), D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
}

void RenderToTexture::createQuadResources()
{
	D3D12_RESOURCE_DESC stBufferResSesc = {};
	stBufferResSesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	stBufferResSesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	stBufferResSesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	stBufferResSesc.Format = DXGI_FORMAT_UNKNOWN;
	stBufferResSesc.Width = 0;
	stBufferResSesc.Height = 1;
	stBufferResSesc.DepthOrArraySize = 1;
	stBufferResSesc.MipLevels = 1;
	stBufferResSesc.SampleDesc.Count = 1;
	stBufferResSesc.SampleDesc.Quality = 0;

	stBufferResSesc.Width = mMVOBufferSize;

	D3D12_HEAP_PROPERTIES stUploadHeapProps = {};
	stUploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
	stUploadHeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	stUploadHeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	stUploadHeapProps.CreationNodeMask = 0;
	stUploadHeapProps.VisibleNodeMask = 0;

	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&stUploadHeapProps
		, D3D12_HEAP_FLAG_NONE
		, &stBufferResSesc
		, D3D12_RESOURCE_STATE_GENERIC_READ
		, nullptr
		, IID_PPV_ARGS(&mCBMVO)));

	ThrowIfFailed(mCBMVO->Map(0, nullptr, reinterpret_cast<void**>(&mMVOBuffer)));

	d3dSetDebugName(mCBMVO.Get(), "mCBMVO");

	D3D12_DESCRIPTOR_HEAP_DESC stHeapDesc = {};
	stHeapDesc.NumDescriptors = 2;
	stHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	stHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	//SRV堆
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&stHeapDesc, IID_PPV_ARGS(&mDHQuad)));

	d3dSetDebugName(mDHQuad.Get(), "mDHQuad");

	// SRV
	D3D12_SHADER_RESOURCE_VIEW_DESC stSRVDesc = {};
	stSRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	stSRVDesc.Format = emRTFormat;
	stSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	stSRVDesc.Texture2D.MipLevels = 1;

	// 踩了个巨坑，因为初始化顺序问题，这里的renderTexture.renderTarget实际上是没有初始化的，导致渲染到纹理始终是黑色
	assert(renderTexture.renderTarget != nullptr);

	// 使用渲染目标作为Shader的纹理资源
	md3dDevice->CreateShaderResourceView(renderTexture.renderTarget.Get(), &stSRVDesc, mDHQuad->GetCPUDescriptorHandleForHeapStart());

	// CBV
	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
	cbvDesc.BufferLocation = mCBMVO->GetGPUVirtualAddress();
	cbvDesc.SizeInBytes = static_cast<uint32_t>(mMVOBufferSize);

	D3D12_CPU_DESCRIPTOR_HANDLE stSRVCBVHandle = mDHQuad->GetCPUDescriptorHandleForHeapStart();
	stSRVCBVHandle.ptr += nSRVDescriptorSize;

	assert(nSRVDescriptorSize != 0); // 如果断言触发，说明没初始化

	md3dDevice->CreateConstantBufferView(&cbvDesc, stSRVCBVHandle);

	//Sample
	stHeapDesc.NumDescriptors = 1;  //只有一个Sample
	stHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;

	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&stHeapDesc, IID_PPV_ARGS(&mDHSampleQuad)));

	d3dSetDebugName(mDHSampleQuad.Get(), "mDHSampleQuad");

	// Sample View
	D3D12_SAMPLER_DESC stSamplerDesc = {};
	stSamplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	stSamplerDesc.MinLOD = 0;
	stSamplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
	stSamplerDesc.MipLODBias = 0.0f;
	stSamplerDesc.MaxAnisotropy = 1;
	stSamplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	stSamplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	stSamplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	stSamplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;

	md3dDevice->CreateSampler(&stSamplerDesc, mDHSampleQuad->GetCPUDescriptorHandleForHeapStart());

	ComPtr<ID3D12Resource> texUpload;

	D3D12_RESOURCE_DESC texDesc = renderTexture.renderTarget->GetDesc();

	auto uploadProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

	auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(BUFFER_SIZE * BUFFER_SIZE * 4);

	md3dDevice->CreateCommittedResource(
		&uploadProps,
		D3D12_HEAP_FLAG_NONE,
		&bufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&texUpload));

	D3D12_SUBRESOURCE_DATA subData = {};
	const uint32_t texWidth  = (uint32_t)texDesc.Width;
	const uint32_t texHeight = (uint32_t)texDesc.Height;
	std::vector<uint32_t> test(texWidth * texHeight);

	// 测试图案
	for (uint32_t y = 0; y < texHeight; y++)
	{
		for (uint32_t x = 0; x < texWidth; x++)
		{
			test[y*texWidth + x] =
				((x/16 + y/16) % 2) ? 0xFF0000FF : 0xFFFFFFFF; // 蓝白棋盘格
		}
	}

	subData.pData      = test.data();
	subData.RowPitch   = texWidth * 4;
	subData.SlicePitch = subData.RowPitch * texHeight;
	
	UpdateSubresources(mCommandList.Get(), renderTexture.renderTarget.Get(), texUpload.Get(), 0, 0, 1, &subData);

	CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
    renderTexture.renderTarget.Get(),
    D3D12_RESOURCE_STATE_COPY_DEST,
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	mCommandList->ResourceBarrier(1, &barrier);

	FlushCommandQueue();
}

void RenderToTexture::OnResize()
{
	D3DApp::OnResize();
}

void RenderToTexture::Update(const GameTimer& gt)
{
	mTimeCurrent = ::GetTickCount();
	//计算旋转的角度：旋转角度(弧度) = 时间(秒) * 角速度(弧度/秒)
	//下面这句代码相当于经典游戏消息循环中的OnUpdate函数中需要做的事情
	dModelRotationYAngle += ((mTimeCurrent - mTimeFrameStart) / 1000.0f) * mPalstance;

	mTimeFrameStart = mTimeCurrent;

	//旋转角度是2PI周期的倍数，去掉周期数，只留下相对0弧度开始的小于2PI的弧度即可
	if (dModelRotationYAngle > XM_2PI)
	{
		dModelRotationYAngle = fmod(dModelRotationYAngle, XM_2PI);
	}

	auto view = glm::lookAtLH(float3(0.0f, 10.0f, -20.0f), float3(0.0f), float3(0.0f, 1.0f, 0.0f));
	auto projection = glm::perspectiveLH(glm::pi<float>() * 0.25f, (float)mClientWidth / (float)mClientHeight, 0.1f, 100.0f);

	auto rotateSkybox = glm::rotate(glm::mat4(1.0f), dModelRotationYAngle, float3(0.0, 1.0f, 0.0f));

	auto mvpSkybox = projection * view * rotateSkybox;

	mvpSkybox = glm::inverse(mvpSkybox);

	mMVPBufSkybox->m_MVP = mvpSkybox;

	auto rotate = glm::rotate(glm::mat4(1.0f), dModelRotationYAngle, float3(0.0, 1.0f, 0.0f));

	// auto mvp = projection * view * rotate;
	auto mvp = projection * view * rotate;

	mMVPBuffer->m_MVP = mvp;

	mMVOBuffer->m_MVO = mxOrthographic;

	D3D12_UpdateScene(&scene, gt.TotalTime());
	
	processInput();

 	updateSceneBuffers();
}

void RenderToTexture::Draw(const GameTimer& gt)
{
	{
		// Reuse the memory associated with command recording.
		// We can only reset when the associated command lists have finished execution on the GPU.
		ThrowIfFailed(mDirectCmdListAlloc->Reset());

		// A command list can be reset after it has been added to the command queue via ExecuteCommandList.
		// Reusing the command list reuses memory.
		ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

		// 第一遍渲染，绘制到纹理
		// 设置屏障将渲染目标纹理从资源转换为渲染目标状态
		renderTexture.begin(mCommandList, mClientWidth, mClientHeight);

		// Skybox
		ID3D12DescriptorHeap* ppHeapsSkybox[] = { mSRVHeapSkybox.Get(), mSamplerHeap.Get() };
		mCommandList->SetDescriptorHeaps(_countof(ppHeapsSkybox), ppHeapsSkybox);
		
		// mCommandList->ExecuteBundle(mBundlesSkybox.Get());

		mCommandList->SetPipelineState(mPipelineStateSkybox.Get());
		mCommandList->SetGraphicsRootSignature(mRootSignatureSkybox.Get());

		//设置SRV
		CD3DX12_GPU_DESCRIPTOR_HANDLE stGPUCBVHandleSkybox(mSRVHeapSkybox->GetGPUDescriptorHandleForHeapStart()
		, 1
		, nSRVDescriptorSize);

		mCommandList->SetGraphicsRootDescriptorTable(0, stGPUCBVHandleSkybox);

		MaterialBuffer buffer = 
		{
			.textureID = 0,
			.samplerID = 0
		};

		uint32_t count = sizeof(buffer) / sizeof(uint32_t);

		mCommandList->SetGraphicsRoot32BitConstants(1, count, &buffer, 0);

		mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		mCommandList->IASetVertexBuffers(0, 1, &mVertexBufferViewSkybox);

		//Draw Call！！！
		mCommandList->DrawInstanced((uint32_t)mSkyboxVertices.size(), 1, 0, 0);


		// Cube
		ID3D12DescriptorHeap* ppHeaps[] = { mSRVHeap.Get(), mSamplerHeap.Get() };

		mCommandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
		
		// mCommandList->ExecuteBundle(mBundlesCube.Get());

		mCommandList->SetGraphicsRootSignature(mRootSignature.Get());
		mCommandList->SetPipelineState(mPipelineState.Get());

		buffer = 
		{
			.textureID = 0,
			.samplerID = 0
		};

		mCommandList->SetGraphicsRoot32BitConstants(0, count, &buffer, 0);

		CD3DX12_GPU_DESCRIPTOR_HANDLE stGPUCBVHandle(mSRVHeap->GetGPUDescriptorHandleForHeapStart()
		, 3
		, nSRVDescriptorSize);

		mCommandList->SetGraphicsRootDescriptorTable(1, stGPUCBVHandle);

		mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		mCommandList->IASetVertexBuffers(0, 1, &mVertexBufferView);
		mCommandList->IASetIndexBuffer(&mIndexBufferView);

		mCommandList->DrawIndexedInstanced(mIndexCount, 1, 0, 0, 0);
		
		// Earth
		mCommandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

		mCommandList->ExecuteBundle(mBundlesEarth.Get());

		renderTexture.end(mCommandList);

		// Done recording commands.
		ThrowIfFailed(mCommandList->Close());

		// Add the command list to the queue for execution.
		ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
		mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

		FlushCommandQueue();
	}

		auto view = glm::lookAtLH(float3(0.0f, 0.0f, -20.0f), float3(0.0f), float3(0.0f, 1.0f, 0.0f));
		auto projection = glm::perspectiveLH(glm::pi<float>() * 0.25f, (float)mClientWidth / (float)mClientHeight, 0.1f, 100.0f);

		auto rotateSkybox = glm::rotate(glm::mat4(1.0f), dModelRotationYAngle, float3(0.0, 1.0f, 0.0f));

		auto mvpSkybox = projection * view * rotateSkybox;

		mvpSkybox = glm::inverse(mvpSkybox);

		mMVPBufSkybox->m_MVP = mvpSkybox;

		auto rotate = glm::rotate(glm::mat4(1.0f), dModelRotationYAngle, float3(0.0, 1.0f, 0.0f));

		// auto mvp = projection * view * rotate;
		auto mvp = projection * view * rotate;

		mMVPBuffer->m_MVP = mvp;

	{
		// 渲染第二遍
		// Reuse the memory associated with command recording.
		// We can only reset when the associated command lists have finished execution on the GPU.
		ThrowIfFailed(mDirectCmdListAlloc->Reset());

		// A command list can be reset after it has been added to the command queue via ExecuteCommandList.
		// Reusing the command list reuses memory.
		ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

		auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
			D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

		// Indicate a state transition on the resource usage.
		mCommandList->ResourceBarrier(1, &barrier);

		// mCommandList->SetGraphicsRootSignature(mIRootSignature.Get());
		// ID3D12DescriptorHeap* ppHeaps[] = { mSRVHeap.Get(), mSamplerHeap.Get() };
		// mCommandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

		// mCommandList->SetGraphicsRootDescriptorTable(0, mSRVHeap->GetGPUDescriptorHandleForHeapStart());
		// mCommandList->SetGraphicsRootDescriptorTable(1, mSamplerHeap->GetGPUDescriptorHandleForHeapStart());

		// CD3DX12_GPU_DESCRIPTOR_HANDLE stGPUCBVHandle(mSRVHeap->GetGPUDescriptorHandleForHeapStart()
		// , 2
		// , md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));

		// mCommandList->SetGraphicsRootDescriptorTable(2, stGPUCBVHandle);

		// mCommandList->SetPipelineState(mPipelineState.Get());

		// Set the viewport and scissor rect.  This needs to be reset whenever the command list is reset.
		mCommandList->RSSetViewports(1, &mScreenViewport);
		mCommandList->RSSetScissorRects(1, &mScissorRect);

		// Clear the back buffer and depth buffer.
		mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::CornflowerBlue, 0, nullptr);
		mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

		// Specify the buffers we are going to render to.
		auto currentView = CurrentBackBufferView();
		auto depthStencilView = DepthStencilView();
		mCommandList->OMSetRenderTargets(1, &currentView, true, &depthStencilView);

		// mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		// mCommandList->IASetVertexBuffers(0, 1, &mVertexBufferView);
		// mCommandList->IASetIndexBuffer(&mIndexBufferView);

		// mCommandList->DrawIndexedInstanced(mIndexCount, 1, 0, 0, 0);

		// Skybox
		ID3D12DescriptorHeap* ppHeapsSkybox[] = { mSRVHeapSkybox.Get(), mSamplerHeap.Get() };
		mCommandList->SetDescriptorHeaps(_countof(ppHeapsSkybox), ppHeapsSkybox);
		
		// mCommandList->ExecuteBundle(mBundlesSkybox.Get());

		mCommandList->SetPipelineState(mPipelineStateSkybox.Get());
		mCommandList->SetGraphicsRootSignature(mRootSignatureSkybox.Get());

		//设置SRV
		CD3DX12_GPU_DESCRIPTOR_HANDLE stGPUCBVHandleSkybox(mSRVHeapSkybox->GetGPUDescriptorHandleForHeapStart()
		, 1
		, nSRVDescriptorSize);

		mCommandList->SetGraphicsRootDescriptorTable(0, stGPUCBVHandleSkybox);

		MaterialBuffer buffer = 
		{
			.textureID = 0,
			.samplerID = 0
		};

		uint32_t count = sizeof(buffer) / sizeof(uint32_t);

		mCommandList->SetGraphicsRoot32BitConstants(1, count, &buffer, 0);

		mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		mCommandList->IASetVertexBuffers(0, 1, &mVertexBufferViewSkybox);

		//Draw Call！！！
		mCommandList->DrawInstanced((uint32_t)mSkyboxVertices.size(), 1, 0, 0);

		// // Cube
		// ID3D12DescriptorHeap* ppHeaps[] = { mSRVHeap.Get(), mSamplerHeap.Get() };

		// mCommandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

		// // mCommandList->ExecuteBundle(mBundlesCube.Get());

		// mCommandList->SetGraphicsRootSignature(mRootSignature.Get());
		// mCommandList->SetPipelineState(mPipelineState.Get());

		// buffer = 
		// {
		// 	.textureID = 0,
		// 	.samplerID = 0
		// };

		// mCommandList->SetGraphicsRoot32BitConstants(0, count, &buffer, 0);

		// CD3DX12_GPU_DESCRIPTOR_HANDLE stGPUCBVHandle(mSRVHeap->GetGPUDescriptorHandleForHeapStart()
		// , 3
		// , nSRVDescriptorSize);

		// mCommandList->SetGraphicsRootDescriptorTable(1, stGPUCBVHandle);

		// mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		// mCommandList->IASetVertexBuffers(0, 1, &mVertexBufferView);
		// mCommandList->IASetIndexBuffer(&mIndexBufferView);

		// mCommandList->DrawIndexedInstanced(mIndexCount, 1, 0, 0, 0);
		
		// // Earth
		// mCommandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

		// mCommandList->ExecuteBundle(mBundlesEarth.Get());

		ID3D12DescriptorHeap* ppHeaps[] = { mSRVHeap.Get(), mSamplerHeap.Get() };

		mCommandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

		mCommandList->SetGraphicsRootSignature(mRootSignature.Get());
		mCommandList->SetPipelineState(mPipelineState.Get());

		for (const auto& actor : actors)
		{
			const Mesh& mesh = meshes[actor.renderData.meshID];

			buffer = 
			{
				.textureID = actor.renderData.meshID,
				.samplerID = 0,
				.objectIndex = actor.renderData.objectIndex
			};

			mCommandList->SetGraphicsRoot32BitConstants(0, count, &buffer, 0);

			CD3DX12_GPU_DESCRIPTOR_HANDLE stGPUCBVHandle(mSRVHeap->GetGPUDescriptorHandleForHeapStart()
			, 3
			, nSRVDescriptorSize);

			mCommandList->SetGraphicsRootDescriptorTable(1, stGPUCBVHandle);

			mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			mCommandList->IASetVertexBuffers(0, 1, &mesh.vertexBufferView);
			mCommandList->IASetIndexBuffer(&mesh.indexBufferView);

			mCommandList->DrawIndexedInstanced(mesh.indexCount, 1, 0, 0, 0);
		}

		// Quad
		ID3D12DescriptorHeap* ppHeapsQuad[] = { mDHQuad.Get(), mDHSampleQuad.Get() };

		mCommandList->SetDescriptorHeaps(_countof(ppHeapsQuad), ppHeapsQuad);

		mCommandList->SetGraphicsRootSignature(mRSQuad.Get());

		mCommandList->SetPipelineState(mPSOQuad.Get());

		D3D12_GPU_DESCRIPTOR_HANDLE stGPUCBVHandleQuad(mDHQuad->GetGPUDescriptorHandleForHeapStart());

		stGPUCBVHandleQuad.ptr += nSRVDescriptorSize;

		mCommandList->SetGraphicsRootDescriptorTable(0, stGPUCBVHandleQuad);

		mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		mCommandList->IASetVertexBuffers(0, 1, &mVBViewQuad);

		// Draw Call！！！
		mCommandList->DrawInstanced(mQuadVertexCnt, 1, 0, 0);

		// DrawBindless();
 
		// Indicate a state transition on the resource usage.
		barrier = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
			D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
		mCommandList->ResourceBarrier(1, &barrier);

		// Done recording commands.
		ThrowIfFailed(mCommandList->Close());

		// Add the command list to the queue for execution.
		ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
		mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);
	}

	// ThrowIfFailed(md3dDevice->GetDeviceRemovedReason());

	// swap the back and front buffers
	ThrowIfFailed(mSwapChain->Present(0, 0));

	//---------------------------------------------------------------------------------------------
	//获取新的后缓冲序号，因为Present真正完成时后缓冲的序号就更新了

	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

	// Wait until frame commands are complete.  This waiting is inefficient and is
	// done for simplicity.  Later we will show how to organize our rendering code
	// so we do not have to wait per frame.
	FlushCommandQueue();
}

void RenderToTexture::DrawBindless()
{
	upload_arena.Reset();

	mCommandList->SetDescriptorHeaps(1, &g_d3d.cbv_srv_uav.heap);
	mCommandList->SetGraphicsRootSignature(g_d3d.rs_bindless);

	D3D12_INDEX_BUFFER_VIEW ibv = {
		.BufferLocation = scene.ibuffer->GetGPUVirtualAddress(),
		.SizeInBytes    = 3 * sizeof(uint16_t),
		.Format         = DXGI_FORMAT_R16_UINT,
	};

	mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	mCommandList->IASetIndexBuffer(&ibv);

	//------------------------------------------------------------------------
	// Set PSO

	mCommandList->SetPipelineState(scene.pso);

	//------------------------------------------------------------------------
	// Set pass constants

	D3D12_BufferAllocation pass_alloc = 
		upload_arena.Allocate(
			sizeof(D3D12_PassConstants), 
			D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);

	D3D12_PassConstants *pass_constants = (D3D12_PassConstants *)pass_alloc.cpu_base;
	pass_constants->vbuffer_srv = scene.vbuffer_srv.index;

	mCommandList->SetGraphicsRootConstantBufferView(D3D12_RootParameter_pass_cbv, pass_alloc.gpu_base);

	//------------------------------------------------------------------------
	// Draw

	for (size_t i = 0; i < scene.triangle_guy_count; i++)
	{
		TriangleGuy *guy = &scene.triangle_guys[i];

		//------------------------------------------------------------------------
		// Set root constants

		uint32_t texture_index = (guy->texture + scene.texture_index_offset) % 4;

		D3D12_RootConstants root_constants = {
			.offset        = guy->position,
			.texture_index = scene.textures_srvs[texture_index].index,
		};

		uint32_t uint_count = sizeof(root_constants) / sizeof(uint32_t);
		mCommandList->SetGraphicsRoot32BitConstants(D3D12_RootParameter_32bit_constants, uint_count, &root_constants, 0);

		//------------------------------------------------------------------------
		// Draw the triangle

		mCommandList->DrawIndexedInstanced(3, 1, 0, 0, 0);
	}
}

void RenderToTexture::processInput()
{
	if (GetAsyncKeyState(VK_SPACE))
	{
		actorIndex = (actorIndex + 1) % actors.size();
	}
}

void RenderToTexture::updateSceneBuffers()
{
	beginCopy();

	int32_t index = 0;

	for (auto& buffer : mSceneBuffers)
	{
		buffer.viewProjectionMatrix = mMVPBuffer->m_MVP * actors[index].transform.WorldMatrix();
		index += 1;
	}

	memcpy(mappedSceneBuffer, mSceneBuffers.data(), sceneBufferSize);

	mUploadCommandList->CopyBufferRegion(
		objectBuffer.Get(), 0,
		uploadBuffer.Get(), 0,
		sceneBufferSize);

	endCopy();
}

void RenderToTexture::beginCopy()
{
	mUploadCommandList->ResourceBarrier(1, &beginCopyBarrier);
}

void RenderToTexture::endCopy()
{
	mUploadCommandList->ResourceBarrier(1, &endCopyBarrier);
	
	executeUploadCommandList();
	FlushCommandQueue();
	resetUploadCommandList();
}