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

//for WIC
#include <wincodec.h>

//linker
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "d3dcompiler.lib")

using namespace DirectX;
using namespace Microsoft;
using namespace Microsoft::WRL;

//新定义的宏用于上取整除法
#define GRS_UPPER_DIV(A,B) ((UINT)(((A)+((B)-1))/(B)))

//更简洁的向上边界对齐算法 内存管理中常用 请记住
#define GRS_UPPER(A,B) ((UINT)(((A)+((B)-1))&~(B - 1)))

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
	XMFLOAT3 m_vPos;		//Position
	XMFLOAT2 m_vTxc;		//Texcoord
	XMFLOAT3 m_vNor;		//Normal
};

struct FrameMVPBuffer
{
	XMFLOAT4X4 m_MVP;	//经典的Model-view-projection(MVP)矩阵.
};

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

class D3D12TextureCube : public D3DApp
{
public:
	D3D12TextureCube(HINSTANCE hInstance);
	~D3D12TextureCube();

    virtual bool Initialize() override;

    void createRootSignature();
	void createHeap();
	void createShaders();
	void createPipelineStateObject();
    void createVertexBuffer();
    void createIndexBuffer();
	void createTexture();
    void createShaderResourceView();
	void createConstBufferView();
	DXGI_FORMAT loadTexture2D(const WCHAR* pszTexcuteFileName, DXGI_FORMAT& stTextureFormat, ComPtr<ID3D12Heap>& pITextureHeap, ComPtr<ID3D12Resource>& pITexture, ComPtr<ID3D12Resource>& pITextureUpload , ComPtr<ID3D12Heap>& pIUploadHeap, ComPtr<ID3D12CommandQueue>& pICommandQueue, ComPtr<ID3D12GraphicsCommandList>& pICommandList, ComPtr<ID3D12CommandAllocator>& pICommandAllocator, ComPtr<ID3D12PipelineState>& pIPipelineState, ComPtr<ID3D12Fence>& pIFence);

	void loadShaderBinary(const std::string& shaderBaseName, ShaderData& shaderData);
private:
	virtual void OnResize()override;
	virtual void Update(const GameTimer& gt)override;
	virtual void Draw(const GameTimer& gt)override;

	DXGI_FORMAT	emRenderTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

	ComPtr<ID3DBlob> mISignatureBlob;
	ComPtr<ID3DBlob> mIErrorBlob;
	ComPtr<ID3D12RootSignature>	mIRootSignature;

	// 编译Shader创建渲染管线状态对象
	ComPtr<ID3DBlob> pIBlobVertexShader;
	ComPtr<ID3DBlob> pIBlobPixelShader;
	ShaderData shader;

	ComPtr<ID3D12PipelineState>	mPipelineState;

	ComPtr<ID3D12Resource> mVertexBuffer;
	ComPtr<ID3D12Resource> mIndexBuffer;

	D3D12_VERTEX_BUFFER_VIEW mVertexBufferView = {};
	D3D12_INDEX_BUFFER_VIEW mIndexBufferView = {};

	float mAspectRatio = 3.0f;

	D3D12_RESOURCE_BARRIER stBeginResBarrier = {};
	D3D12_RESOURCE_BARRIER stEndResBarrier = {};

	ComPtr<ID3D12Heap> mTextureHeap;
	ComPtr<ID3D12Heap> mUploadHeap;
	ComPtr<ID3D12Resource> mCBVUpload;
	ComPtr<ID3D12Resource> mTextureUpload1;
	ComPtr<ID3D12Resource> mTextureUpload2;

	DXGI_FORMAT stTextureFormat = DXGI_FORMAT_UNKNOWN;

	ComPtr<ID3D12Resource> mTexcute1;
	ComPtr<ID3D12Resource> mTexcute2;

	ComPtr<ID3D12DescriptorHeap> mSRVHeap;
	ComPtr<ID3D12DescriptorHeap> mSamplerHeap;

	UINT nSRVDescriptorSize = 0U;
	UINT mVertexCount;
	UINT mIndexCount;

	//初始的默认摄像机的位置
	XMVECTOR Eye = XMVectorSet(0.0f, 0.0f, -20.0f, 0.0f); //眼睛位置
	XMVECTOR At = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);    //眼睛所盯的位置
	XMVECTOR Up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);    //头部正上方位置
														
	double mPalstance = 10.0f * XM_PI / 180.0f;	//物体旋转的角速度，单位：弧度/秒

	float mBoxSize = 3.0f;
	float mTCMax = 3.0f;

	UINT64 mBufferOffset = 0;
	UINT64 mTextureHeapOffset = 0;
	UINT64 mVertexBufferSize = 0;
	const int BUFFER_SIZE = 1024;

	FrameMVPBuffer* mMVPBuffer = nullptr;
	SIZE_T mMVPBufferSize = GRS_UPPER_DIV(sizeof(FrameMVPBuffer), 256) * 256;

	//25、记录帧开始时间，和当前时间，以循环结束为界
	ULONGLONG mTimeFrameStart = ::GetTickCount64();
	ULONGLONG mTimeCurrent = mTimeFrameStart;

	//计算旋转角度需要的变量
	double dModelRotationYAngle = 0.0f;
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
		D3D12TextureCube theApp(hInstance);
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

D3D12TextureCube::D3D12TextureCube(HINSTANCE hInstance)
	: D3DApp(hInstance)
{
}

D3D12TextureCube::~D3D12TextureCube()
{
}

bool D3D12TextureCube::Initialize()
{
	if (!D3DApp::Initialize())
		return false;

	createRootSignature();
	createHeap();
	createShaders();
	createPipelineStateObject();
	createVertexBuffer();
	createIndexBuffer();
	createTexture();
	createShaderResourceView();
	createConstBufferView();

    return true;
}

void D3D12TextureCube::createRootSignature()
{
	D3D12_FEATURE_DATA_ROOT_SIGNATURE stFeatureData = {};
	// 检测是否支持V1.1版本的根签名
	stFeatureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
	if (FAILED(md3dDevice->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &stFeatureData, sizeof(stFeatureData))))
	{
		stFeatureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
	}
	// 在GPU上执行SetGraphicsRootDescriptorTable后，我们不修改命令列表中的SRV，因此我们可以使用默认Rang行为:
	// D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE
	CD3DX12_DESCRIPTOR_RANGE1 stDSPRanges[2];
	
	stDSPRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE);
	stDSPRanges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);

	CD3DX12_DESCRIPTOR_RANGE1 samplerRange[1];
	samplerRange[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 2, 0); // 2 个 sampler，从寄存器 s0 开始

	CD3DX12_ROOT_PARAMETER1 stRootParameters[3];
	stRootParameters[0].InitAsDescriptorTable(1, &stDSPRanges[0], D3D12_SHADER_VISIBILITY_PIXEL);	//SRV仅PS可见
	stRootParameters[1].InitAsDescriptorTable(1, &samplerRange[0], D3D12_SHADER_VISIBILITY_PIXEL);	//SAMPLE仅PS可见
	stRootParameters[2].InitAsDescriptorTable(1, &stDSPRanges[1], D3D12_SHADER_VISIBILITY_ALL);	//CBV是所有Shader可见

	D3D12_STATIC_SAMPLER_DESC stSamplerDesc = {};
	stSamplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
	stSamplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	stSamplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	stSamplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	stSamplerDesc.MipLODBias = 0;
	stSamplerDesc.MaxAnisotropy = 0;
	stSamplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	stSamplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	stSamplerDesc.MinLOD = 0.0f;
	stSamplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
	stSamplerDesc.ShaderRegister = 0;
	stSamplerDesc.RegisterSpace = 0;
	stSamplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_DESCRIPTOR_HEAP_DESC samplerHeapDesc = {};
	samplerHeapDesc.NumDescriptors = 2; // 假设我们有 2 个 sampler
	samplerHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
	samplerHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE; // 必须让着色器可见

	md3dDevice->CreateDescriptorHeap(&samplerHeapDesc, IID_PPV_ARGS(&mSamplerHeap));

	D3D12_SAMPLER_DESC samplerDescs[2] = {};

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

	auto samplerHandle = mSamplerHeap->GetCPUDescriptorHandleForHeapStart();
	UINT samplerDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

	for (int i = 0; i < 2; ++i)
	{
		md3dDevice->CreateSampler(&samplerDescs[i], samplerHandle);
		samplerHandle.ptr += samplerDescriptorSize;
	}

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC stRootSignatureDesc;
	stRootSignatureDesc.Init_1_1(_countof(stRootParameters), stRootParameters
		, 0, nullptr
		, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPtr<ID3DBlob> pISignatureBlob;
	ComPtr<ID3DBlob> pIErrorBlob;
	ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&stRootSignatureDesc
		, stFeatureData.HighestVersion
		, &pISignatureBlob
		, &pIErrorBlob));
	ThrowIfFailed(md3dDevice->CreateRootSignature(0
		, pISignatureBlob->GetBufferPointer()
		, pISignatureBlob->GetBufferSize()
		, IID_PPV_ARGS(&mIRootSignature)));
}

void D3D12TextureCube::createHeap()
{
	D3D12_HEAP_DESC stUploadHeapDesc = {  };
	//尺寸依然是实际纹理数据大小的2倍并64K边界对齐大小
	stUploadHeapDesc.SizeInBytes = GRS_UPPER(BUFFER_SIZE * BUFFER_SIZE * 4 * 10, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
	//注意上传堆肯定是Buffer类型，可以不指定对齐方式，其默认是64k边界对齐
	stUploadHeapDesc.Alignment = 0;
	stUploadHeapDesc.Properties.Type = D3D12_HEAP_TYPE_UPLOAD;		//上传堆类型
	stUploadHeapDesc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	stUploadHeapDesc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	//上传堆就是缓冲，可以摆放任意数据
	stUploadHeapDesc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;

	ThrowIfFailed(md3dDevice->CreateHeap(&stUploadHeapDesc, IID_PPV_ARGS(&mUploadHeap)));

	ThrowIfFailed(md3dDevice->CreatePlacedResource(mUploadHeap.Get()
	, mBufferOffset
	, &CD3DX12_RESOURCE_DESC::Buffer(BUFFER_SIZE * BUFFER_SIZE * 4)
	, D3D12_RESOURCE_STATE_GENERIC_READ
	, nullptr
	, IID_PPV_ARGS(&mTextureUpload1)));


	mBufferOffset += GRS_UPPER(BUFFER_SIZE * BUFFER_SIZE * 4, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

	ThrowIfFailed(md3dDevice->CreatePlacedResource(mUploadHeap.Get()
		, mBufferOffset
		, &CD3DX12_RESOURCE_DESC::Buffer(BUFFER_SIZE* BUFFER_SIZE * 4)
		, D3D12_RESOURCE_STATE_GENERIC_READ
		, nullptr
		, IID_PPV_ARGS(&mTextureUpload2)));

	mBufferOffset += GRS_UPPER(BUFFER_SIZE * BUFFER_SIZE * 4, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

	D3D12_HEAP_DESC stTextureHeapDesc = {};
	//为堆指定纹理图片至少2倍大小的空间，这里没有详细去计算了，只是指定了一个足够大的空间，够放纹理就行
	//实际应用中也是要综合考虑分配堆的大小，以便可以重用堆
	stTextureHeapDesc.SizeInBytes = GRS_UPPER(10 * BUFFER_SIZE * BUFFER_SIZE, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
	//指定堆的对齐方式，这里使用了默认的64K边界对齐，因为我们暂时不需要MSAA支持
	stTextureHeapDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
	stTextureHeapDesc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;		//默认堆类型
	stTextureHeapDesc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	stTextureHeapDesc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	//拒绝渲染目标纹理、拒绝深度蜡板纹理，实际就只是用来摆放普通纹理
	stTextureHeapDesc.Flags = D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES | D3D12_HEAP_FLAG_DENY_BUFFERS;

	ThrowIfFailed(md3dDevice->CreateHeap(&stTextureHeapDesc, IID_PPV_ARGS(&mTextureHeap)));
}

void D3D12TextureCube::createShaders()
{
#if defined(_DEBUG)
    // 调试状态下，打开Shader编译的调试标志，不优化
    UINT nCompileFlags =
        D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    UINT nCompileFlags = 0;
#endif

	//编译为行矩阵形式	   
	nCompileFlags |= D3DCOMPILE_PACK_MATRIX_ROW_MAJOR;

    TCHAR pszAppPath[MAX_PATH] = {};

    if (0 == ::GetModuleFileName(nullptr, pszAppPath, MAX_PATH))
    {
        ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
    }

    WCHAR *lastSlash = _tcsrchr(pszAppPath, _T('\\'));
    if (lastSlash)
    { // 删除Exe文件名
        *(lastSlash) = _T('\0');
    }

    lastSlash = _tcsrchr(pszAppPath, _T('\\'));
    if (lastSlash)
    { // 删除x64路径
        *(lastSlash) = _T('\0');
    }

    lastSlash = _tcsrchr(pszAppPath, _T('\\'));
    if (lastSlash)
    { // 删除Debug 或 Release路径
        *(lastSlash + 1) = _T('\0');
    }

    TCHAR pszShaderFileName[MAX_PATH] = {};

    StringCchPrintf(pszShaderFileName, MAX_PATH, _T("%sD3D12TextureCube\\Shader\\textureCube.hlsl"), pszAppPath);

    ThrowIfFailed(D3DCompileFromFile(pszShaderFileName, nullptr, nullptr, "VSMain", "vs_5_0", nCompileFlags, 0, &pIBlobVertexShader, nullptr));
    ThrowIfFailed(D3DCompileFromFile(pszShaderFileName, nullptr, nullptr, "PSMain", "ps_5_0", nCompileFlags, 0, &pIBlobPixelShader, nullptr));

	loadShaderBinary("Shader/textureCube", shader);
}

void D3D12TextureCube::createPipelineStateObject()
{
    // Define the vertex input layout.
    D3D12_INPUT_ELEMENT_DESC stInputElementDescs[] =
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{"NORMAL", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

    // 定义渲染管线状态描述结构，创建渲染管线对象
    D3D12_GRAPHICS_PIPELINE_STATE_DESC stPSODesc = {};
    stPSODesc.InputLayout = {stInputElementDescs, _countof(stInputElementDescs)};
    stPSODesc.pRootSignature = mIRootSignature.Get();
    // stPSODesc.VS.pShaderBytecode = pIBlobVertexShader->GetBufferPointer();
    // stPSODesc.VS.BytecodeLength = pIBlobVertexShader->GetBufferSize();
    // stPSODesc.PS.pShaderBytecode = pIBlobPixelShader->GetBufferPointer();
    // stPSODesc.PS.BytecodeLength = pIBlobPixelShader->GetBufferSize();

    stPSODesc.VS.pShaderBytecode = shader.vsData.data();
    stPSODesc.VS.BytecodeLength = shader.vsData.size();
    stPSODesc.PS.pShaderBytecode = shader.psData.data();
    stPSODesc.PS.BytecodeLength = shader.psData.size();

    stPSODesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    stPSODesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;

    stPSODesc.BlendState.AlphaToCoverageEnable = FALSE;
    stPSODesc.BlendState.IndependentBlendEnable = FALSE;
    stPSODesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    stPSODesc.DepthStencilState.DepthEnable = FALSE;
    stPSODesc.DepthStencilState.StencilEnable = FALSE;

    stPSODesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    stPSODesc.NumRenderTargets = 1;
    stPSODesc.RTVFormats[0] = emRenderTargetFormat;

    stPSODesc.SampleMask = UINT_MAX;
    stPSODesc.SampleDesc.Count = 1;

    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&stPSODesc, IID_PPV_ARGS(&mPipelineState)));
}

void D3D12TextureCube::createVertexBuffer()
{
    // 定义正方形的3D数据结构
    Vertex stTriangleVertices[] =
        {
			{ {-1.0f * mBoxSize,  1.0f * mBoxSize, -1.0f * mBoxSize}, {0.0f * mTCMax, 0.0f* mTCMax}, {0.0f,  0.0f, -1.0f} },
			{ {1.0f * mBoxSize,  1.0f * mBoxSize, -1.0f * mBoxSize}, {1.0f* mTCMax, 0.0f* mTCMax},  {0.0f,  0.0f, -1.0f} },
			{ {-1.0f * mBoxSize, -1.0f * mBoxSize, -1.0f * mBoxSize}, {0.0f* mTCMax, 1.0f* mTCMax}, {0.0f,  0.0f, -1.0f} },
			{ {-1.0f * mBoxSize, -1.0f * mBoxSize, -1.0f * mBoxSize}, {0.0f * mTCMax, 1.0f * mTCMax}, {0.0f,  0.0f, -1.0f} },
			{ {1.0f * mBoxSize,  1.0f * mBoxSize, -1.0f * mBoxSize}, {1.0f * mTCMax, 0.0f * mTCMax},  {0.0f, 0.0f, -1.0f} },
			{ {1.0f * mBoxSize, -1.0f * mBoxSize, -1.0f * mBoxSize}, {1.0f * mTCMax, 1.0f * mTCMax},  {0.0f,  0.0f, -1.0f} },
			{ {1.0f * mBoxSize,  1.0f * mBoxSize, -1.0f * mBoxSize}, {0.0f * mTCMax, 0.0f * mTCMax},  {1.0f,  0.0f,  0.0f} },
			{ {1.0f * mBoxSize,  1.0f * mBoxSize,  1.0f * mBoxSize}, {1.0f * mTCMax, 0.0f * mTCMax},  {1.0f,  0.0f,  0.0f} },
			{ {1.0f * mBoxSize, -1.0f * mBoxSize, -1.0f * mBoxSize}, {0.0f * mTCMax, 1.0f * mTCMax},  {1.0f,  0.0f,  0.0f} },
			{ {1.0f * mBoxSize, -1.0f * mBoxSize, -1.0f * mBoxSize}, {0.0f * mTCMax, 1.0f * mTCMax},  {1.0f,  0.0f,  0.0f} },
			{ {1.0f * mBoxSize,  1.0f * mBoxSize,  1.0f * mBoxSize}, {1.0f * mTCMax, 0.0f * mTCMax},  {1.0f,  0.0f,  0.0f} },
			{ {1.0f * mBoxSize, -1.0f * mBoxSize,  1.0f * mBoxSize}, {1.0f * mTCMax, 1.0f * mTCMax},  {1.0f,  0.0f,  0.0f} },
			{ {1.0f * mBoxSize,  1.0f * mBoxSize,  1.0f * mBoxSize}, {0.0f * mTCMax, 0.0f * mTCMax},  {0.0f,  0.0f,  1.0f} },
			{ {-1.0f * mBoxSize,  1.0f * mBoxSize,  1.0f * mBoxSize}, {1.0f * mTCMax, 0.0f * mTCMax},  {0.0f,  0.0f,  1.0f} },
			{ {1.0f * mBoxSize, -1.0f * mBoxSize,  1.0f * mBoxSize}, {0.0f * mTCMax, 1.0f * mTCMax}, {0.0f,  0.0f,  1.0f} },
			{ {1.0f * mBoxSize, -1.0f * mBoxSize,  1.0f * mBoxSize}, {0.0f * mTCMax, 1.0f * mTCMax},  {0.0f,  0.0f,  1.0f} },
			{ {-1.0f * mBoxSize,  1.0f * mBoxSize,  1.0f * mBoxSize}, {1.0f * mTCMax, 0.0f * mTCMax},  {0.0f,  0.0f,  1.0f} },
			{ {-1.0f * mBoxSize, -1.0f * mBoxSize,  1.0f * mBoxSize}, {1.0f * mTCMax, 1.0f * mTCMax},  {0.0f,  0.0f,  1.0f} },
			{ {-1.0f * mBoxSize,  1.0f * mBoxSize,  1.0f * mBoxSize}, {0.0f * mTCMax, 0.0f * mTCMax}, {-1.0f,  0.0f,  0.0f} },
			{ {-1.0f * mBoxSize,  1.0f * mBoxSize, -1.0f * mBoxSize}, {1.0f * mTCMax, 0.0f * mTCMax}, {-1.0f,  0.0f,  0.0f} },
			{ {-1.0f * mBoxSize, -1.0f * mBoxSize,  1.0f * mBoxSize}, {0.0f * mTCMax, 1.0f * mTCMax}, {-1.0f,  0.0f,  0.0f} },
			{ {-1.0f * mBoxSize, -1.0f * mBoxSize,  1.0f * mBoxSize}, {0.0f * mTCMax, 1.0f * mTCMax}, {-1.0f,  0.0f,  0.0f} },
			{ {-1.0f * mBoxSize,  1.0f * mBoxSize, -1.0f * mBoxSize}, {1.0f * mTCMax, 0.0f * mTCMax}, {-1.0f,  0.0f,  0.0f} },
			{ {-1.0f * mBoxSize, -1.0f * mBoxSize, -1.0f * mBoxSize}, {1.0f * mTCMax, 1.0f * mTCMax}, {-1.0f,  0.0f,  0.0f} },
			{ {-1.0f * mBoxSize,  1.0f * mBoxSize,  1.0f * mBoxSize}, {0.0f * mTCMax, 0.0f * mTCMax},  {0.0f,  1.0f,  0.0f} },
			{ {1.0f * mBoxSize,  1.0f * mBoxSize,  1.0f * mBoxSize}, {1.0f * mTCMax, 0.0f * mTCMax},  {0.0f,  1.0f,  0.0f} },
			{ {-1.0f * mBoxSize,  1.0f * mBoxSize, -1.0f * mBoxSize}, {0.0f * mTCMax, 1.0f * mTCMax},  {0.0f,  1.0f,  0.0f} },
			{ {-1.0f * mBoxSize,  1.0f * mBoxSize, -1.0f * mBoxSize}, {0.0f * mTCMax, 1.0f * mTCMax},  {0.0f,  1.0f,  0.0f} },
			{ {1.0f * mBoxSize,  1.0f * mBoxSize,  1.0f * mBoxSize}, {1.0f * mTCMax, 0.0f * mTCMax},  {0.0f,  1.0f,  0.0f} },
			{ {1.0f * mBoxSize,  1.0f * mBoxSize, -1.0f * mBoxSize}, {1.0f * mTCMax, 1.0f * mTCMax},  {0.0f,  1.0f,  0.0f }},
			{ {-1.0f * mBoxSize, -1.0f * mBoxSize, -1.0f * mBoxSize}, {0.0f * mTCMax, 0.0f * mTCMax},  {0.0f, -1.0f,  0.0f} },
			{ {1.0f * mBoxSize, -1.0f * mBoxSize, -1.0f * mBoxSize}, {1.0f * mTCMax, 0.0f * mTCMax},  {0.0f, -1.0f,  0.0f} },
			{ {-1.0f * mBoxSize, -1.0f * mBoxSize,  1.0f * mBoxSize}, {0.0f * mTCMax, 1.0f * mTCMax},  {0.0f, -1.0f,  0.0f} },
			{ {-1.0f * mBoxSize, -1.0f * mBoxSize,  1.0f * mBoxSize}, {0.0f * mTCMax, 1.0f * mTCMax},  {0.0f, -1.0f,  0.0f} },
			{ {1.0f * mBoxSize, -1.0f * mBoxSize, -1.0f * mBoxSize}, {1.0f * mTCMax, 0.0f * mTCMax},  {0.0f, -1.0f,  0.0f} },
			{ {1.0f * mBoxSize, -1.0f * mBoxSize,  1.0f * mBoxSize}, {1.0f * mTCMax, 1.0f * mTCMax},  {0.0f, -1.0f,  0.0f} },
        };

    mVertexBufferSize = sizeof(stTriangleVertices);

	mVertexCount = _countof(stTriangleVertices);

    D3D12_HEAP_PROPERTIES stHeapProp = {D3D12_HEAP_TYPE_UPLOAD};

    D3D12_RESOURCE_DESC stResSesc = {};
    stResSesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    stResSesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    stResSesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    stResSesc.Format = DXGI_FORMAT_UNKNOWN;
    stResSesc.Width = mVertexBufferSize;
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
		, IID_PPV_ARGS(&mVertexBuffer)));

    UINT8 *pVertexDataBegin = nullptr;
    D3D12_RANGE stReadRange = { 0, 0 };

	ThrowIfFailed(mVertexBuffer->Map(
		0
		, &stReadRange
		, reinterpret_cast<void**>(&pVertexDataBegin)));

	memcpy(pVertexDataBegin
		, stTriangleVertices
		, sizeof(stTriangleVertices));

	mVertexBuffer->Unmap(0, nullptr);

	mVertexBufferView.BufferLocation = mVertexBuffer->GetGPUVirtualAddress();
	mVertexBufferView.StrideInBytes = sizeof(Vertex);
	mVertexBufferView.SizeInBytes = mVertexBufferSize;
}


void D3D12TextureCube::createIndexBuffer()
{
	UINT32 pBoxIndices[] //立方体索引数组
		= {
		0,1,2,
		3,4,5,

		6,7,8,
		9,10,11,

		12,13,14,
		15,16,17,

		18,19,20,
		21,22,23,

		24,25,26,
		27,28,29,

		30,31,32,
		33,34,35,
	};

	const UINT nszIndexBuffer = sizeof(pBoxIndices);

	mIndexCount = _countof(pBoxIndices);

	//计算边界对齐的正确的偏移位置
	mBufferOffset = GRS_UPPER(mBufferOffset + mVertexBufferSize, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

	CD3DX12_RANGE stReadRange(0, 0);		// We do not intend to read from this resource on the CPU.
	UINT8* pIndexDataBegin = nullptr;

	ThrowIfFailed(md3dDevice->CreatePlacedResource(
			mUploadHeap.Get()
			, mBufferOffset
			, &CD3DX12_RESOURCE_DESC::Buffer(nszIndexBuffer)
			, D3D12_RESOURCE_STATE_GENERIC_READ
			, nullptr
			, IID_PPV_ARGS(&mIndexBuffer)));

	ThrowIfFailed(mIndexBuffer->Map(0, &stReadRange, reinterpret_cast<void**>(&pIndexDataBegin)));
	memcpy(pIndexDataBegin, pBoxIndices, nszIndexBuffer);
	mIndexBuffer->Unmap(0, nullptr);

	mIndexBufferView.BufferLocation = mIndexBuffer->GetGPUVirtualAddress();
	mIndexBufferView.Format = DXGI_FORMAT_R32_UINT;
	mIndexBufferView.SizeInBytes = nszIndexBuffer;

	mBufferOffset = GRS_UPPER(mBufferOffset + nszIndexBuffer, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
}

void  D3D12TextureCube::createConstBufferView()
{
	// 创建常量缓冲 注意缓冲尺寸设置为256边界对齐大小
	ThrowIfFailed(md3dDevice->CreatePlacedResource(
		mUploadHeap.Get()
		, mBufferOffset
		, &CD3DX12_RESOURCE_DESC::Buffer(mMVPBufferSize)
		, D3D12_RESOURCE_STATE_GENERIC_READ
		, nullptr
		, IID_PPV_ARGS(&mCBVUpload)));

	// Map 之后就不再Unmap了 直接复制数据进去 这样每帧都不用map-copy-unmap浪费时间了
	ThrowIfFailed(mCBVUpload->Map(0, nullptr, reinterpret_cast<void**>(&mMVPBuffer)));

	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
	cbvDesc.BufferLocation = mCBVUpload->GetGPUVirtualAddress();
	cbvDesc.SizeInBytes = static_cast<UINT>(mMVPBufferSize);

	CD3DX12_CPU_DESCRIPTOR_HANDLE cbvSrvHandle(mSRVHeap->GetCPUDescriptorHandleForHeapStart()
		, 2
		, md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));

	md3dDevice->CreateConstantBufferView(&cbvDesc, cbvSrvHandle);
}

DXGI_FORMAT D3D12TextureCube::loadTexture2D(const WCHAR* pszTexcuteFileName, DXGI_FORMAT& stTextureFormat, ComPtr<ID3D12Heap>& pITextureHeap, ComPtr<ID3D12Resource>& pITexture, ComPtr<ID3D12Resource>& pITextureUpload , ComPtr<ID3D12Heap>& pIUploadHeap, ComPtr<ID3D12CommandQueue>& pICommandQueue, ComPtr<ID3D12GraphicsCommandList>& pICommandList, ComPtr<ID3D12CommandAllocator>& pICommandAllocator, ComPtr<ID3D12PipelineState>& pIPipelineState, ComPtr<ID3D12Fence>& pIFence)
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

	UINT nTextureW;
	UINT nTextureH;

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

	UINT nBPP = 0;

	// 到这里终于可以得到BPP了，这也是我看的比较吐血的地方，为了BPP居然饶了这么多环节
	ThrowIfFailed(pIWICPixelinfo->GetBitsPerPixel(&nBPP));

	// 计算图片实际的行大小（单位：字节），这里使用了一个上取整除法即（A+B-1）/B ，
	// 这曾经被传说是微软的面试题,希望你已经对它了如指掌
	uint64_t nPicRowPitch = (uint64_t(nTextureW) * uint64_t(nBPP) + 7u) / 8u;

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
		pITextureHeap.Get()
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
	void* pbPicData = ::HeapAlloc(::GetProcessHeap(), HEAP_ZERO_MEMORY, n64UploadBufferSize);
	if (nullptr == pbPicData)
	{
		throw CGRSCOMException(HRESULT_FROM_WIN32(GetLastError()));
	}

	//从图片中读取出数据
	ThrowIfFailed(pIBMP->CopyPixels(nullptr
		, nPicRowPitch
		, static_cast<UINT>(nPicRowPitch * nTextureH)   //注意这里才是图片数据真实的大小，这个值通常小于缓冲的大小
		, reinterpret_cast<BYTE*>(pbPicData)));

	//获取向上传堆拷贝纹理数据的一些纹理转换尺寸信息
	//对于复杂的DDS纹理这是非常必要的过程
	UINT64 n64RequiredSize = 0u;
	UINT   nNumSubresources = 1u;  //我们只有一副图片，即子资源个数为1
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT stTxtLayouts = {};
	UINT64 n64TextureRowSizes = 0u;
	UINT   nTextureRowNum = 0u;

	D3D12_RESOURCE_DESC stDestDesc = pITexture->GetDesc();

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
	ThrowIfFailed(pITextureUpload->Map(0, NULL, reinterpret_cast<void**>(&pData)));

	BYTE* pDestSlice = reinterpret_cast<BYTE*>(pData) + stTxtLayouts.Offset;
	const BYTE* pSrcSlice = reinterpret_cast<const BYTE*>(pbPicData);
	for (UINT y = 0; y < nTextureRowNum; ++y)
	{
		memcpy(pDestSlice + static_cast<SIZE_T>(stTxtLayouts.Footprint.RowPitch) * y
			, pSrcSlice + static_cast<SIZE_T>(nPicRowPitch) * y
			, nPicRowPitch);
	}
	//取消映射 对于易变的数据如每帧的变换矩阵等数据，可以撒懒不用Unmap了，
	//让它常驻内存,以提高整体性能，因为每次Map和Unmap是很耗时的操作
	//因为现在起码都是64位系统和应用了，地址空间是足够的，被长期占用不会影响什么
	pITextureUpload->Unmap(0, NULL);

	//释放图片数据，做一个干净的程序员
	::HeapFree(::GetProcessHeap(), 0, pbPicData);

	//---------------------------------------------------------------------------------------------
	//命令分配器先Reset一下，刚才已经执行过了一个复制纹理的命令
	ThrowIfFailed(pICommandAllocator->Reset());
	//Reset命令列表，并重新指定命令分配器和PSO对象
	ThrowIfFailed(pICommandList->Reset(pICommandAllocator.Get(), pIPipelineState.Get()));
	//---------------------------------------------------------------------------------------------

	//向命令队列发出从上传堆复制纹理数据到默认堆的命令
	CD3DX12_TEXTURE_COPY_LOCATION Dst1(pITexture.Get(), 0);
	CD3DX12_TEXTURE_COPY_LOCATION Src1(pITextureUpload.Get(), stTxtLayouts);
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

	UINT64 n64FenceValue = 0ui64;

	//---------------------------------------------------------------------------------------------
	// 19、等待纹理资源正式复制完成先
	const UINT64 fence = n64FenceValue;
	ThrowIfFailed(pICommandQueue->Signal(pIFence.Get(), fence));
	n64FenceValue++;

	//---------------------------------------------------------------------------------------------
	// 看命令有没有真正执行到围栏标记的这里，没有就利用事件去等待，注意使用的是命令队列对象的指针
	if (pIFence->GetCompletedValue() < fence)
	{
		ThrowIfFailed(pIFence->SetEventOnCompletion(fence, hFenceEvent));
		WaitForSingleObject(hFenceEvent, INFINITE);
	}

	return stTextureFormat;
}

void D3D12TextureCube::loadShaderBinary(const std::string& shaderBaseName, ShaderData& shaderData)
{	
	std::string vsName = shaderBaseName + "_VS.dxil";
	std::string psName = shaderBaseName + "_PS.dxil";

	std::ifstream fs(vsName, std::ios::binary | std::ios::ate);
    size_t size = fs.tellg();
    shaderData.vsData.resize(size);
    fs.seekg(0);
    fs.read((char*)shaderData.vsData.data(), size);

	fs.close();

	fs.open(psName, std::ios::binary | std::ios::ate);

	size = fs.tellg();
    shaderData.psData.resize(size);
    fs.seekg(0);
    fs.read((char*)shaderData.psData.data(), size);
}

void D3D12TextureCube::createTexture()
{
	loadTexture2D(_T("Texture/bear.jpg"), stTextureFormat, mTextureHeap, mTexcute1, mTextureUpload1, mUploadHeap, mCommandQueue, mCommandList, mDirectCmdListAlloc, mPipelineState, mFence);
	loadTexture2D(_T("Texture/bear_flipX.jpg"), stTextureFormat, mTextureHeap, mTexcute2, mTextureUpload2, mUploadHeap, mCommandQueue, mCommandList, mDirectCmdListAlloc, mPipelineState, mFence);
}

void D3D12TextureCube::createShaderResourceView()
{
	D3D12_DESCRIPTOR_HEAP_DESC stSRVHeapDesc = {};
	stSRVHeapDesc.NumDescriptors = 3; // 2 SRV + 1 CBV
	stSRVHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	stSRVHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&stSRVHeapDesc, IID_PPV_ARGS(&mSRVHeap)));

    //---------------------------------------------------------------------------------------------
    // 最终创建SRV描述符
    D3D12_SHADER_RESOURCE_VIEW_DESC stSRVDesc = {};
    stSRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    stSRVDesc.Format = stTextureFormat;
    stSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    stSRVDesc.Texture2D.MipLevels = 1;

    nSRVDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    CD3DX12_CPU_DESCRIPTOR_HANDLE stSRVHandle(mSRVHeap->GetCPUDescriptorHandleForHeapStart());

    md3dDevice->CreateShaderResourceView(mTexcute1.Get(), &stSRVDesc, stSRVHandle);

    stSRVHandle.Offset(1, nSRVDescriptorSize);

    md3dDevice->CreateShaderResourceView(mTexcute2.Get(), &stSRVDesc, stSRVHandle);
}

void D3D12TextureCube::OnResize()
{
	D3DApp::OnResize();
}

void D3D12TextureCube::Update(const GameTimer& gt)
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

	//模型矩阵 model
	XMMATRIX xmRot = XMMatrixRotationY(static_cast<float>(dModelRotationYAngle));

	//计算 模型矩阵 model * 视矩阵 view
	XMMATRIX xmMVP = XMMatrixMultiply(xmRot, XMMatrixLookAtLH(Eye, At, Up));

	//投影矩阵 projection
	xmMVP = XMMatrixMultiply(xmMVP, (XMMatrixPerspectiveFovLH(XM_PIDIV4, (FLOAT)mClientWidth / (FLOAT)mClientHeight, 0.1f, 1000.0f)));

	XMStoreFloat4x4(&mMVPBuffer->m_MVP, xmMVP);
}

void D3D12TextureCube::Draw(const GameTimer& gt)
{
	// Reuse the memory associated with command recording.
	// We can only reset when the associated command lists have finished execution on the GPU.
	ThrowIfFailed(mDirectCmdListAlloc->Reset());

	// A command list can be reset after it has been added to the command queue via ExecuteCommandList.
	// Reusing the command list reuses memory.
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	// Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	mCommandList->SetGraphicsRootSignature(mIRootSignature.Get());
	ID3D12DescriptorHeap* ppHeaps[] = { mSRVHeap.Get(), mSamplerHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

	mCommandList->SetGraphicsRootDescriptorTable(0, mSRVHeap->GetGPUDescriptorHandleForHeapStart());
	mCommandList->SetGraphicsRootDescriptorTable(1, mSamplerHeap->GetGPUDescriptorHandleForHeapStart());

	CD3DX12_GPU_DESCRIPTOR_HANDLE stGPUCBVHandle(mSRVHeap->GetGPUDescriptorHandleForHeapStart()
	, 2
	, md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));

	mCommandList->SetGraphicsRootDescriptorTable(2, stGPUCBVHandle);

	mCommandList->SetPipelineState(mPipelineState.Get());

	// Set the viewport and scissor rect.  This needs to be reset whenever the command list is reset.
	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	// Clear the back buffer and depth buffer.
	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::CornflowerBlue, 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
	mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	mCommandList->IASetVertexBuffers(0, 1, &mVertexBufferView);
	mCommandList->IASetIndexBuffer(&mIndexBufferView);

	// Specify the buffers we are going to render to.
	mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	mCommandList->DrawIndexedInstanced(mIndexCount, 1, 0, 0, 0);

	// Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	// Done recording commands.
	ThrowIfFailed(mCommandList->Close());

	// Add the command list to the queue for execution.
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

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
