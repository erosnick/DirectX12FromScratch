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
#include "../Common/glm/glm.hpp"
#include "../Common/glm/vec3.hpp" // glm::vec3
#include "../Common/glm/vec4.hpp" // glm::vec4
#include "../Common/glm/mat4x4.hpp" // glm::mat4
#include "../Common/glm/ext/matrix_transform.hpp" // glm::translate, glm::rotate, glm::scale
#include "../Common/glm/ext/matrix_clip_space.hpp" // glm::perspective
#include "../Common/glm/ext/scalar_constants.hpp" // glm::pi

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
	glm::vec3 m_vPos;		//Position
	glm::vec2 m_vTex;		//Texcoord
	glm::vec3 m_vNor;		//Normal
};

struct SkyboxVertex
{
	//天空盒子的顶点结构
	glm::vec4 m_vPos;
};

struct FrameMVPBufferGLM
{
	glm::mat4 m_MVP;	//经典的Model-view-projection(MVP)矩阵.
	glm::mat4 m_mWorld;
	glm::vec4 m_v4EyePos;
};

struct MaterialBuffer
{
	uint32_t textureID;
};

struct WICTranslate
{
	GUID wic;
	DXGI_FORMAT format;
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

class SkyBox : public D3DApp
{
public:
	SkyBox(HINSTANCE hInstance);
	~SkyBox();

    virtual bool Initialize() override;

	void createDXCLibrary();
	void createCommandAllocators();
    void createRootSignature();
	void createHeaps();
	void createShaders();
	void createPipelineStateObjects();
    void createVertexBuffer();
    void createIndexBuffer();
	void createEarthVertexBuffer();
	void createEarthIndexBuffer();
	void createSkyboxVertexBuffer();
	void createTexture();
    void createShaderResourceView();
	void createConstBufferView();
	void recordBundles();

	DXGI_FORMAT loadTexture2D(const WCHAR* pszTexcuteFileName, DXGI_FORMAT& stTextureFormat, ComPtr<ID3D12Resource>& pITexture, ComPtr<ID3D12Resource>& pITextureUpload , ComPtr<ID3D12CommandQueue>& pICommandQueue, ComPtr<ID3D12GraphicsCommandList>& pICommandList, ComPtr<ID3D12CommandAllocator>& pICommandAllocator, ComPtr<ID3D12PipelineState>& pIPipelineState, ComPtr<ID3D12Fence>& pIFence);
	void loadShader(const std::wstring& path, ComPtr<ID3DBlob>& vertexShader, ComPtr<ID3DBlob>& pixelShader);
	void loadShader(const std::wstring& path, ComPtr<IDxcBlob>& shaderCode, const std::wstring& entryPoint = L"VSMain", const std::wstring& targetProfile = L"vs_6_0");

	void createHeap(ComPtr<ID3D12Heap>& heap, uint64_t size, D3D12_HEAP_TYPE type = D3D12_HEAP_TYPE_UPLOAD, D3D12_HEAP_FLAGS flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS);
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
	ComPtr<ID3D12Resource> mTextureUpload;
	ComPtr<ID3D12Resource> mTextureUploadSkybox;

	DXGI_FORMAT stTextureFormat = DXGI_FORMAT_UNKNOWN;

	ComPtr<ID3D12Resource> mTextureBear;
	ComPtr<ID3D12Resource> mTextureSkybox;
	ComPtr<ID3D12Resource> mTextureEarth;
	ComPtr<ID3D12Resource> mTextureJupiter;

	ComPtr<ID3D12DescriptorHeap> mSRVHeap;
	ComPtr<ID3D12DescriptorHeap> mSRVHeapSkybox;
	ComPtr<ID3D12DescriptorHeap> mSamplerHeap;

	UINT nSRVDescriptorSize = 0U;
	UINT mVertexCount;
	UINT mIndexCount;

	//初始的默认摄像机的位置
	glm::vec3 mEyePos = glm::vec3(0.0f, 0.0f, -20.0f); //眼睛位置
	glm::vec3 mLockAt = glm::vec3(0.0f, 0.0f, 0.0f);    //眼睛所盯的位置
	glm::vec3 mHeapUp = glm::vec3(0.0f, 1.0f, 0.0f);    //头部正上方位置
				
	float mYaw = 0.0f;				// 绕正Z轴的旋转量.
	float mPitch = 0.0f;			// 绕XZ平面的旋转量

	double mPalstance = 10.0f * glm::pi<float>() / 180.0f;	//物体旋转的角速度，单位：弧度/秒

	float mBoxSize = 3.0f;
	float mTCMax = 1.0f;

	UINT64 mBufferOffset = 0;
	UINT64 mTextureHeapOffset = 0;
	UINT64 mVertexBufferSize = 0;
	const int BUFFER_SIZE = 1024;

	FrameMVPBufferGLM* mMVPBufferGLM = nullptr;
	FrameMVPBufferGLM* mMVPBufSkyboxGLM = nullptr;
	MaterialBuffer* mMaterialBuffer = nullptr;

	//常量缓冲区大小上对齐到256Bytes边界
	SIZE_T mMVPBufferSize = GRS_UPPER_DIV(sizeof(FrameMVPBufferGLM), 256) * 256;
	SIZE_T mMaterialBufferSize = GRS_UPPER_DIV(sizeof(MaterialBuffer), 256) * 256;

	float mSphereSize = 1.0f;

	//25、记录帧开始时间，和当前时间，以循环结束为界
	ULONGLONG mTimeFrameStart = ::GetTickCount64();
	ULONGLONG mTimeCurrent = mTimeFrameStart;

	//计算旋转角度需要的变量
	float dModelRotationYAngle = 0.0f;

	//======================================================================================================
	//加载Skybox的Cube Map需要的变量
	std::unique_ptr<uint8_t[]> ddsData;
	std::vector<D3D12_SUBRESOURCE_DATA> subresources;
	DDS_ALPHA_MODE emAlphaMode = DDS_ALPHA_MODE_UNKNOWN;
	bool bIsCube = false;
	//======================================================================================================

	//球体的网格数据
	Vertex* mSphereVertices = nullptr;
	UINT mSphereVertexCnt = 0;
	UINT* mSphereIndices = nullptr;
	UINT mSphereIndexCnt = 0;
 
	//Sky Box的网格数据
	UINT mSkyboxIndexCnt = 4;
	SkyboxVertex mSkyboxVertices[4] = {};

	ComPtr<ID3D12CommandAllocator>		mCommandAllocatorSkybox;
	ComPtr<ID3D12CommandAllocator>		mCommandAllocatorEarth;
	ComPtr<ID3D12CommandAllocator>		mCommandAllocatorCube;
	ComPtr<ID3D12GraphicsCommandList>   mBundlesSkybox;
	ComPtr<ID3D12GraphicsCommandList>   mBundlesEarth;
	ComPtr<ID3D12GraphicsCommandList>   mBundlesCube;

	ComPtr<IDxcLibrary> mDXCLibrary;
	ComPtr<IDxcCompiler> mCompiler;
	ComPtr<IDxcBlob> mDxcVertexShader;
	ComPtr<IDxcBlob> mDxcPixelShader;

	bool mSupportDXIL = true;
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
		SkyBox theApp(hInstance);
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

SkyBox::SkyBox(HINSTANCE hInstance)
	: D3DApp(hInstance)
{
}

SkyBox::~SkyBox()
{
}

bool SkyBox::Initialize()
{
	if (!D3DApp::Initialize())
		return false;

	createDXCLibrary();
	createCommandAllocators();
	createRootSignature();
	createHeaps();
	createShaders();
	createPipelineStateObjects();
	createVertexBuffer();
	createIndexBuffer();
	createEarthVertexBuffer();
	createEarthIndexBuffer();
	createSkyboxVertexBuffer();
	createTexture();
	createShaderResourceView();
	createConstBufferView();
	recordBundles();

    return true;
}

void SkyBox::loadShader(const std::wstring& path, ComPtr<ID3DBlob>& vertexShader, ComPtr<ID3DBlob>& pixelShader)
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

    ThrowIfFailed(D3DCompileFromFile(path.c_str(), nullptr, nullptr, "VSMain", "vs_5_1", nCompileFlags, 0, &vertexShader, nullptr));
    ThrowIfFailed(D3DCompileFromFile(path.c_str(), nullptr, nullptr, "PSMain", "ps_5_1", nCompileFlags, 0, &pixelShader, nullptr));
}

void SkyBox::loadShader(const std::wstring& path, ComPtr<IDxcBlob>& shaderCode, const std::wstring& entryPoint, const std::wstring& targetProfile)
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

void SkyBox::createHeap(ComPtr<ID3D12Heap>& heap, uint64_t size, D3D12_HEAP_TYPE type, D3D12_HEAP_FLAGS flags)
{
	D3D12_HEAP_DESC stUploadHeapDesc = {  };
	//尺寸依然是实际纹理数据大小的2倍并64K边界对齐大小
	stUploadHeapDesc.SizeInBytes = GRS_UPPER(size, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
	//注意上传堆肯定是Buffer类型，可以不指定对齐方式，其默认是64k边界对齐
	stUploadHeapDesc.Alignment = 0;
	stUploadHeapDesc.Properties.Type = type;		//上传堆类型
	stUploadHeapDesc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	stUploadHeapDesc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	//上传堆就是缓冲，可以摆放任意数据
	stUploadHeapDesc.Flags = flags;

	ThrowIfFailed(md3dDevice->CreateHeap(&stUploadHeapDesc, IID_PPV_ARGS(&heap)));
}

void SkyBox::createDXCLibrary()
{
	ThrowIfFailed(DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(&mDXCLibrary)));

	ThrowIfFailed(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&mCompiler)));
}

void SkyBox::createCommandAllocators()
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
}

void SkyBox::createRootSignature()
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
	samplerRange[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 3, 0); // 3 个 sampler，从寄存器 s0 开始

	CD3DX12_ROOT_PARAMETER1 stRootParameters[4];
	stRootParameters[0].InitAsDescriptorTable(1, &stDSPRanges[0], D3D12_SHADER_VISIBILITY_PIXEL);	//SRV仅PS可见
	stRootParameters[1].InitAsDescriptorTable(1, &samplerRange[0], D3D12_SHADER_VISIBILITY_PIXEL);	//SAMPLE仅PS可见
	stRootParameters[2].InitAsDescriptorTable(1, &stDSPRanges[1], D3D12_SHADER_VISIBILITY_ALL);		//CBV是所有Shader可见
	stRootParameters[3].InitAsConstants(1, 1);

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
	samplerHeapDesc.NumDescriptors = 3; // 假设我们有 3 个 sampler
	samplerHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
	samplerHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE; // 必须让着色器可见

	md3dDevice->CreateDescriptorHeap(&samplerHeapDesc, IID_PPV_ARGS(&mSamplerHeap));

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
	UINT samplerDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

	for (int i = 0; i < 3; ++i)
	{
		md3dDevice->CreateSampler(&samplerDescs[i], samplerHandle);
		samplerHandle.ptr += samplerDescriptorSize;
	}

	// Earth
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
		, IID_PPV_ARGS(&mRootSignature)));

	// Skybox
	// 在GPU上执行SetGraphicsRootDescriptorTable后，我们不修改命令列表中的SRV，因此我们可以使用默认Rang行为:
	// D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE
	CD3DX12_DESCRIPTOR_RANGE1 stDSPRangesSkybox[2];
	stDSPRangesSkybox[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE);
	stDSPRangesSkybox[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);

	CD3DX12_DESCRIPTOR_RANGE1 samplerRangeSkybox[1];
	samplerRangeSkybox[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 2, 0); // 1 个 sampler，从寄存器 s0 开始

	CD3DX12_ROOT_PARAMETER1 stRootParametersSkybox[3];
	stRootParametersSkybox[0].InitAsDescriptorTable(1, &stDSPRangesSkybox[0], D3D12_SHADER_VISIBILITY_PIXEL);		//SRV仅PS可见
	stRootParametersSkybox[1].InitAsDescriptorTable(1, &samplerRangeSkybox[0], D3D12_SHADER_VISIBILITY_PIXEL);	//SAMPLE仅PS可见
	stRootParametersSkybox[2].InitAsDescriptorTable(1, &stDSPRangesSkybox[1], D3D12_SHADER_VISIBILITY_ALL);	//CBV是所有Shader可见

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC stRootSignatureDescSkybox;
	stRootSignatureDescSkybox.Init_1_1(_countof(stRootParametersSkybox), stRootParametersSkybox
		, 0, nullptr
		, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

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
}

void SkyBox::createHeaps()
{
	createHeap(mUploadHeap, BUFFER_SIZE * BUFFER_SIZE * 4 * 10);

	ThrowIfFailed(md3dDevice->CreatePlacedResource(mUploadHeap.Get()
	, mBufferOffset
	, &CD3DX12_RESOURCE_DESC::Buffer(BUFFER_SIZE * BUFFER_SIZE * 4)
	, D3D12_RESOURCE_STATE_GENERIC_READ
	, nullptr
	, IID_PPV_ARGS(&mTextureUpload)));

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

void SkyBox::createShaders()
{
	loadShader(L"shader/textureCube.hlsl", mBlobVertexShader, mBlobPixelShader);
	loadShader(L"shader/skybox.hlsl", mBlobVertexShaderSkybox, mBlobPixelShaderSkybox);

	D3D12_FEATURE_DATA_D3D12_OPTIONS7 opt7{};
	md3dDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &opt7, sizeof(opt7));

	mSupportDXIL = opt7.MeshShaderTier != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED;

	loadShader(L"shader/textureCubeDxc.hlsl", mDxcVertexShader, L"VSMain", L"vs_6_0");
	loadShader(L"shader/textureCubeDxc.hlsl", mDxcPixelShader, L"PSMain", L"ps_6_0");
}

void SkyBox::createPipelineStateObjects()
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
    stPSODesc.pRootSignature = mRootSignature.Get();
	
	if (mSupportDXIL)
	{
		stPSODesc.VS.pShaderBytecode = mDxcVertexShader->GetBufferPointer();
		stPSODesc.VS.BytecodeLength = mDxcVertexShader->GetBufferSize();
		stPSODesc.PS.pShaderBytecode = mDxcPixelShader->GetBufferPointer();
		stPSODesc.PS.BytecodeLength = mDxcPixelShader->GetBufferSize();
	}
	else
	{
		stPSODesc.VS.pShaderBytecode = mBlobVertexShader->GetBufferPointer();
		stPSODesc.VS.BytecodeLength = mBlobVertexShader->GetBufferSize();
		stPSODesc.PS.pShaderBytecode = mBlobPixelShader->GetBufferPointer();
		stPSODesc.PS.BytecodeLength = mBlobPixelShader->GetBufferSize();
	}

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

	// 天空盒子只有顶点只有位置参数
	D3D12_INPUT_ELEMENT_DESC stIALayoutSkybox[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	// 创建Skybox的(PSO)对象 注意天空盒子不需要深度测试
	stPSODesc.InputLayout = { stIALayoutSkybox, _countof(stIALayoutSkybox) };
    stPSODesc.pRootSignature = mRootSignatureSkybox.Get();
	stPSODesc.VS = CD3DX12_SHADER_BYTECODE(mBlobVertexShaderSkybox.Get());
	stPSODesc.PS = CD3DX12_SHADER_BYTECODE(mBlobPixelShaderSkybox.Get());
	stPSODesc.DepthStencilState.DepthEnable = FALSE;
	stPSODesc.DepthStencilState.StencilEnable = FALSE;

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&stPSODesc, IID_PPV_ARGS(&mPipelineStateSkybox)));
}

void SkyBox::createVertexBuffer()
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

	mBufferOffset = GRS_UPPER(mBufferOffset + mVertexBufferSize, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
}


void SkyBox::createIndexBuffer()
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

void SkyBox::createEarthVertexBuffer()
{
	//22、加载球体的网格数据
	{
		std::ifstream fin;
		char input;

		fin.open("Texture/sphere.txt");
		if (fin.fail())
		{
			throw CGRSCOMException(E_FAIL);
		}
		fin.get(input);
		while (input != ':')
		{
			fin.get(input);
		}
		fin >> mSphereVertexCnt;
		mSphereIndexCnt = mSphereVertexCnt;
		fin.get(input);
		while (input != ':')
		{
			fin.get(input);
		}
		fin.get(input);
		fin.get(input);

		mSphereVertices = (Vertex*)HeapAlloc(::GetProcessHeap()
			, HEAP_ZERO_MEMORY
			, mSphereVertexCnt * sizeof(Vertex));
		mSphereIndices = (UINT*)HeapAlloc(::GetProcessHeap()
			, HEAP_ZERO_MEMORY
			, mSphereVertexCnt * sizeof(Vertex));

		for (UINT i = 0; i < mSphereVertexCnt; i++)
		{
			fin >> mSphereVertices[i].m_vPos.x >> mSphereVertices[i].m_vPos.y >> mSphereVertices[i].m_vPos.z;
			fin >> mSphereVertices[i].m_vTex.x >> mSphereVertices[i].m_vTex.y;

			//pstSphereVertices[i].m_vTex.x = fabs(pstSphereVertices[i].m_vTex.x);
			//pstSphereVertices[i].m_vTex.x *= 0.5f;

			fin >> mSphereVertices[i].m_vNor.x >> mSphereVertices[i].m_vNor.y >> mSphereVertices[i].m_vNor.z;

			mSphereIndices[i] = i;
		}
	}


	//---------------------------------------------------------------------------------------------
	//使用定位方式在相同的上传堆上以“定位方式”创建顶点缓冲，注意第二个参数指出了堆中的偏移位置
	//按照堆边界对齐的要求，我们主动将偏移位置对齐到了64k的边界上
	ThrowIfFailed(md3dDevice->CreatePlacedResource(
		mUploadHeap.Get()
		, mBufferOffset
		, &CD3DX12_RESOURCE_DESC::Buffer(mSphereVertexCnt * sizeof(Vertex))
		, D3D12_RESOURCE_STATE_GENERIC_READ
		, nullptr
		, IID_PPV_ARGS(&mVertexBufferEarth)));

	//使用map-memcpy-unmap大法将数据传至顶点缓冲对象
	//注意顶点缓冲使用是和上传纹理数据缓冲相同的一个堆，这很神奇
	UINT8* pVertexDataBegin = nullptr;
	CD3DX12_RANGE stReadRange(0, 0);		// We do not intend to read from this resource on the CPU.

	ThrowIfFailed(mVertexBufferEarth->Map(0, &stReadRange, reinterpret_cast<void**>(&pVertexDataBegin)));
	memcpy(pVertexDataBegin, mSphereVertices, mSphereVertexCnt * sizeof(Vertex));
	mVertexBufferEarth->Unmap(0, nullptr);

	//创建资源视图，实际可以简单理解为指向顶点缓冲的显存指针
	mVertexBufferViewEarth.BufferLocation = mVertexBufferEarth->GetGPUVirtualAddress();
	mVertexBufferViewEarth.StrideInBytes = sizeof(Vertex);
	mVertexBufferViewEarth.SizeInBytes = mSphereVertexCnt * sizeof(Vertex);

	//计算边界对齐的正确的偏移位置
	mBufferOffset = GRS_UPPER(mBufferOffset + mSphereVertexCnt * sizeof(Vertex), D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
}

void SkyBox::createEarthIndexBuffer()
{
	ThrowIfFailed(md3dDevice->CreatePlacedResource(
		mUploadHeap.Get()
		, mBufferOffset
		, &CD3DX12_RESOURCE_DESC::Buffer(mSphereIndexCnt * sizeof(UINT))
		, D3D12_RESOURCE_STATE_GENERIC_READ
		, nullptr
		, IID_PPV_ARGS(&mIndexBufferEarth)));

	UINT8* pIndexDataBegin = nullptr;
	CD3DX12_RANGE stReadRange(0, 0);		// We do not intend to read from this resource on the CPU.
	ThrowIfFailed(mIndexBufferEarth->Map(0, &stReadRange, reinterpret_cast<void**>(&pIndexDataBegin)));
	memcpy(pIndexDataBegin, mSphereIndices, mSphereIndexCnt * sizeof(UINT));
	mIndexBufferEarth->Unmap(0, nullptr);

	mIndexBufferViewEarth.BufferLocation = mIndexBufferEarth->GetGPUVirtualAddress();
	mIndexBufferViewEarth.Format = DXGI_FORMAT_R32_UINT;
	mIndexBufferViewEarth.SizeInBytes = mSphereIndexCnt * sizeof(UINT);

	mBufferOffset = GRS_UPPER(mBufferOffset + mSphereIndexCnt * sizeof(UINT), D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
}

void SkyBox::createSkyboxVertexBuffer()
{
		//21、定义Sky的3D数据结构
	{
		float scale = 1.0f;
		float fHighW = -1.0f * scale - (1.0f / (float)mClientWidth);
		float fHighH = -1.0f * scale - (1.0f / (float)mClientHeight);
		float fLowW = 1.0f * scale + (1.0f / (float)mClientWidth);
		float fLowH = 1.0f * scale + (1.0f / (float)mClientHeight);

		mSkyboxVertices[0].m_vPos = glm::vec4(fLowW, fLowH, 1.0f, 1.0f);
		mSkyboxVertices[1].m_vPos = glm::vec4(fLowW, fHighH, 1.0f, 1.0f);
		mSkyboxVertices[2].m_vPos = glm::vec4(fHighW, fLowH, 1.0f, 1.0f);
		mSkyboxVertices[3].m_vPos = glm::vec4(fHighW, fHighH, 1.0f, 1.0f);
	}

	ThrowIfFailed(md3dDevice->CreatePlacedResource(
				mUploadHeap.Get()
				, mBufferOffset
				, &CD3DX12_RESOURCE_DESC::Buffer(mSkyboxIndexCnt * sizeof(SkyboxVertex))
				, D3D12_RESOURCE_STATE_GENERIC_READ
				, nullptr
				, IID_PPV_ARGS(&mVertexBufferSkybox)));
 
	UINT8 *pVertexDataBegin = nullptr;
    D3D12_RANGE stReadRange = { 0, 0 };

	//使用map-memcpy-unmap大法将数据传至顶点缓冲对象
	//注意顶点缓冲使用是和上传纹理数据缓冲相同的一个堆，这很神奇
	ThrowIfFailed(mVertexBufferSkybox->Map(0, &stReadRange, reinterpret_cast<void**>(&pVertexDataBegin)));
	memcpy(pVertexDataBegin, &mSkyboxVertices, mSkyboxIndexCnt * sizeof(SkyboxVertex));
	mVertexBufferSkybox->Unmap(0, nullptr);

	//创建资源视图，实际可以简单理解为指向顶点缓冲的显存指针
	mVertexBufferViewSkybox.BufferLocation = mVertexBufferSkybox->GetGPUVirtualAddress();
	mVertexBufferViewSkybox.StrideInBytes = sizeof(SkyboxVertex);
	mVertexBufferViewSkybox.SizeInBytes = mSkyboxIndexCnt * sizeof(SkyboxVertex);

	mBufferOffset = GRS_UPPER(mBufferOffset + mSkyboxIndexCnt * sizeof(SkyboxVertex), D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);	
}

void SkyBox::recordBundles()
{
	// Cube
	uint32_t objectIndex = 1;

	mBundlesCube->SetGraphicsRootSignature(mRootSignature.Get());
	mBundlesCube->SetPipelineState(mPipelineState.Get());

	ID3D12DescriptorHeap* ppHeaps[] = { mSRVHeap.Get(), mSamplerHeap.Get() };
	mBundlesCube->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

	mBundlesCube->SetGraphicsRootDescriptorTable(0, mSRVHeap->GetGPUDescriptorHandleForHeapStart());
	mBundlesCube->SetGraphicsRootDescriptorTable(1, mSamplerHeap->GetGPUDescriptorHandleForHeapStart());

	CD3DX12_GPU_DESCRIPTOR_HANDLE stGPUCBVHandle(mSRVHeap->GetGPUDescriptorHandleForHeapStart()
	, 2
	, md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));

	mBundlesCube->SetGraphicsRootDescriptorTable(2, stGPUCBVHandle);
	mBundlesCube->SetGraphicsRoot32BitConstants(3, 1, &objectIndex, 0);

	mBundlesCube->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	mBundlesCube->IASetVertexBuffers(0, 1, &mVertexBufferView);
	mBundlesCube->IASetIndexBuffer(&mIndexBufferView);

	mBundlesCube->DrawIndexedInstanced(mIndexCount, 1, 0, 0, 0);

	ThrowIfFailed(mBundlesCube->Close());

	// Earth
	objectIndex = 0;

	mBundlesEarth->SetGraphicsRootSignature(mRootSignature.Get());
	mBundlesEarth->SetPipelineState(mPipelineState.Get());

	mBundlesEarth->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

	mBundlesEarth->SetGraphicsRootDescriptorTable(0, mSRVHeap->GetGPUDescriptorHandleForHeapStart());
	mBundlesEarth->SetGraphicsRootDescriptorTable(1, mSamplerHeap->GetGPUDescriptorHandleForHeapStart());
	mBundlesEarth->SetGraphicsRootDescriptorTable(2, stGPUCBVHandle);
	mBundlesEarth->SetGraphicsRoot32BitConstants(3, 1, &objectIndex, 0);

	mBundlesEarth->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	mBundlesEarth->IASetVertexBuffers(0, 1, &mVertexBufferViewEarth);
	mBundlesEarth->IASetIndexBuffer(&mIndexBufferViewEarth);

	mBundlesEarth->DrawIndexedInstanced(mSphereIndexCnt, 1, 0, 0, 0);

	ThrowIfFailed(mBundlesEarth->Close());

	// Skybox
	//=================================================================================================
	//Skybox的捆绑包
	mBundlesSkybox->SetPipelineState(mPipelineStateSkybox.Get());
	mBundlesSkybox->SetGraphicsRootSignature(mRootSignatureSkybox.Get());
	
	ID3D12DescriptorHeap* ppHeapsSkybox[] = { mSRVHeapSkybox.Get(), mSamplerHeap.Get() };
	mBundlesSkybox->SetDescriptorHeaps(_countof(ppHeapsSkybox), ppHeapsSkybox);

	//设置SRV
	mBundlesSkybox->SetGraphicsRootDescriptorTable(0, mSRVHeapSkybox->GetGPUDescriptorHandleForHeapStart());

	CD3DX12_GPU_DESCRIPTOR_HANDLE stGPUSamplerHandle(mSamplerHeap->GetGPUDescriptorHandleForHeapStart()
	, 2
	, md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER));

	mBundlesSkybox->SetGraphicsRootDescriptorTable(1, stGPUSamplerHandle);

	CD3DX12_GPU_DESCRIPTOR_HANDLE stGPUCBVHandleSkybox(mSRVHeapSkybox->GetGPUDescriptorHandleForHeapStart()
	, 1
	, md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));

	mBundlesSkybox->SetGraphicsRootDescriptorTable(2, stGPUCBVHandleSkybox);

	mBundlesSkybox->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	mBundlesSkybox->IASetVertexBuffers(0, 1, &mVertexBufferViewSkybox);

	//Draw Call！！！
	mBundlesSkybox->DrawInstanced(_countof(mSkyboxVertices), 1, 0, 0);
	mBundlesSkybox->Close();
	//=================================================================================================
}

DXGI_FORMAT SkyBox::loadTexture2D(const WCHAR* pszTexcuteFileName, DXGI_FORMAT& stTextureFormat, ComPtr<ID3D12Resource>& pITexture, ComPtr<ID3D12Resource>& pITextureUpload , ComPtr<ID3D12CommandQueue>& pICommandQueue, ComPtr<ID3D12GraphicsCommandList>& pICommandList, ComPtr<ID3D12CommandAllocator>& pICommandAllocator, ComPtr<ID3D12PipelineState>& pIPipelineState, ComPtr<ID3D12Fence>& pIFence)
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

void SkyBox::createTexture()
{
	loadTexture2D(_T("Texture/bear_flipX.jpg"), stTextureFormat, mTextureBear, mTextureUpload, mCommandQueue, mCommandList, mDirectCmdListAlloc, mPipelineState, mFence);
	loadTexture2D(_T("Texture/金星.jpg"), stTextureFormat, mTextureEarth, mTextureUpload, mCommandQueue, mCommandList, mDirectCmdListAlloc, mPipelineState, mFence);
	loadTexture2D(_T("Texture/木星.jpg"), stTextureFormat, mTextureJupiter, mTextureUpload, mCommandQueue, mCommandList, mDirectCmdListAlloc, mPipelineState, mFence);

	//12、使用DDSLoader辅助函数加载Skybox的纹理
	{
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

		ID3D12Resource* pIResSkyBox = nullptr;
		ThrowIfFailed(LoadDDSTextureFromFile(
			md3dDevice.Get()
			, pszSkyboxTextureFile
			, &pIResSkyBox
			, ddsData
			, subresources
			, SIZE_MAX
			, &emAlphaMode
			, &bIsCube));

		//上面函数加载的纹理在隐式默认堆上，两个Copy动作需要我们自己完成
		mTextureSkybox.Attach(pIResSkyBox);
	}

	uint64_t uploadBufferSkyboxSize = GetRequiredIntermediateSize(mTextureSkybox.Get(), 0, static_cast<UINT>(subresources.size()));

	ThrowIfFailed(md3dDevice->CreatePlacedResource(mUploadHeap.Get()
	, mBufferOffset
	, &CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSkyboxSize)
	, D3D12_RESOURCE_STATE_GENERIC_READ
	, nullptr
	, IID_PPV_ARGS(&mTextureUploadSkybox)));

	mBufferOffset = GRS_UPPER(mBufferOffset + uploadBufferSkyboxSize, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

	//---------------------------------------------------------------------------------------------
	//命令分配器先Reset一下，刚才已经执行过了一个复制纹理的命令
	ThrowIfFailed(mDirectCmdListAlloc->Reset());
	//Reset命令列表，并重新指定命令分配器和PSO对象
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), mPipelineState.Get()));
	//---------------------------------------------------------------------------------------------

	//18、上传Skybox的纹理
	{
		UpdateSubresources(mCommandList.Get()
			, mTextureSkybox.Get()
			, mTextureUploadSkybox.Get()
			, 0
			, 0
			, static_cast<UINT>(subresources.size())
			, subresources.data());

		mCommandList->ResourceBarrier(1
			, &CD3DX12_RESOURCE_BARRIER::Transition(mTextureSkybox.Get()
				, D3D12_RESOURCE_STATE_COPY_DEST
				, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
	}

		//20、执行第二个Copy命令并确定所有的纹理都上传到了默认堆中
	{
		//---------------------------------------------------------------------------------------------
		// 执行命令列表并等待纹理资源上传完成，这一步是必须的
		ThrowIfFailed(mCommandList->Close());
		ID3D12CommandList* ppCommandLists[] = { mCommandList.Get() };
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
}

void SkyBox::createShaderResourceView()
{
	// Earth
	D3D12_DESCRIPTOR_HEAP_DESC stSRVHeapDesc = {};
	stSRVHeapDesc.NumDescriptors = 3; // 2 SRV + 1 CBV
	stSRVHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	stSRVHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&stSRVHeapDesc, IID_PPV_ARGS(&mSRVHeap)));

	// Skybox
	stSRVHeapDesc.NumDescriptors = 2; // 1 SRV + 1 CBV
	stSRVHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	stSRVHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&stSRVHeapDesc, IID_PPV_ARGS(&mSRVHeapSkybox)));

    //---------------------------------------------------------------------------------------------
    // 最终创建SRV描述符
    D3D12_SHADER_RESOURCE_VIEW_DESC stSRVDesc = {};
    stSRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    stSRVDesc.Format = stTextureFormat;
    stSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    stSRVDesc.Texture2D.MipLevels = 1;

    nSRVDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    CD3DX12_CPU_DESCRIPTOR_HANDLE stSRVHandle(mSRVHeap->GetCPUDescriptorHandleForHeapStart());

    md3dDevice->CreateShaderResourceView(mTextureEarth.Get(), &stSRVDesc, stSRVHandle);

    stSRVHandle.Offset(1, nSRVDescriptorSize);

    md3dDevice->CreateShaderResourceView(mTextureJupiter.Get(), &stSRVDesc, stSRVHandle);

	stSRVHandle.Offset(1, nSRVDescriptorSize);

    md3dDevice->CreateShaderResourceView(mTextureBear.Get(), &stSRVDesc, stSRVHandle);

	D3D12_RESOURCE_DESC stDescSkybox = mTextureSkybox->GetDesc();
	stSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
	stSRVDesc.Format = stDescSkybox.Format;
	stSRVDesc.TextureCube.MipLevels = stDescSkybox.MipLevels;

	md3dDevice->CreateShaderResourceView(mTextureSkybox.Get(), &stSRVDesc, mSRVHeapSkybox->GetCPUDescriptorHandleForHeapStart());
}

void  SkyBox::createConstBufferView()
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
	ThrowIfFailed(mCBVUpload->Map(0, nullptr, reinterpret_cast<void**>(&mMVPBufferGLM)));

	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
	cbvDesc.BufferLocation = mCBVUpload->GetGPUVirtualAddress();
	cbvDesc.SizeInBytes = static_cast<UINT>(mMVPBufferSize);

	CD3DX12_CPU_DESCRIPTOR_HANDLE cbvSrvHandle(mSRVHeap->GetCPUDescriptorHandleForHeapStart()
		, 2
		, md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));

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
	// cbvDesc.SizeInBytes = static_cast<UINT>(mMaterialBufferSize);

	// cbvSrvHandle.Offset(1, md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));

	// md3dDevice->CreateConstantBufferView(&cbvDesc, cbvSrvHandle);

	// mBufferOffset = GRS_UPPER(mBufferOffset + mMaterialBufferSize * sizeof(SkyboxVertex), D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

	// 创建常量缓冲 注意缓冲尺寸设置为256边界对齐大小
	ThrowIfFailed(md3dDevice->CreatePlacedResource(
		mUploadHeap.Get()
		, mBufferOffset
		, &CD3DX12_RESOURCE_DESC::Buffer(mMVPBufferSize)
		, D3D12_RESOURCE_STATE_GENERIC_READ
		, nullptr
		, IID_PPV_ARGS(&mCBVUploadSkybox)));

	// Map 之后就不再Unmap了 直接复制数据进去 这样每帧都不用map-copy-unmap浪费时间了
	ThrowIfFailed(mCBVUploadSkybox->Map(0, nullptr, reinterpret_cast<void**>(&mMVPBufSkyboxGLM)));

	cbvDesc.BufferLocation = mCBVUploadSkybox->GetGPUVirtualAddress();
	cbvDesc.SizeInBytes = static_cast<UINT>(mMVPBufferSize);

	CD3DX12_CPU_DESCRIPTOR_HANDLE cbvSrvHandleSkybox(mSRVHeapSkybox->GetCPUDescriptorHandleForHeapStart()
		, 1
		, md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));

	md3dDevice->CreateConstantBufferView(&cbvDesc, cbvSrvHandleSkybox);

	mBufferOffset = GRS_UPPER(mBufferOffset + mMVPBufferSize * sizeof(SkyboxVertex), D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
}

void SkyBox::OnResize()
{
	D3DApp::OnResize();
}

void SkyBox::Update(const GameTimer& gt)
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

	auto view = glm::lookAtLH(glm::vec3(0.0f, 0.0f, -20.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	auto projection = glm::perspectiveLH(glm::pi<float>() * 0.25f, (float)mClientWidth / (float)mClientHeight, 0.1f, 100.0f);

	auto rotateSkybox = glm::rotate(glm::mat4(1.0f), dModelRotationYAngle, glm::vec3(0.0, 1.0f, 0.0f));

	auto mvpSkybox = projection * view * rotateSkybox;

	mvpSkybox = glm::inverse(mvpSkybox);

	mMVPBufSkyboxGLM->m_MVP = mvpSkybox;

	auto rotate = glm::rotate(glm::mat4(1.0f), dModelRotationYAngle, glm::vec3(0.0, 1.0f, 0.0f));

	auto mvp = projection * view * rotate;

	mMVPBufferGLM->m_MVP = mvp;
}

void SkyBox::Draw(const GameTimer& gt)
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
	mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	// mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	// mCommandList->IASetVertexBuffers(0, 1, &mVertexBufferView);
	// mCommandList->IASetIndexBuffer(&mIndexBufferView);

	// mCommandList->DrawIndexedInstanced(mIndexCount, 1, 0, 0, 0);

	ID3D12DescriptorHeap* ppHeapsSkybox[] = { mSRVHeapSkybox.Get(), mSamplerHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(ppHeapsSkybox), ppHeapsSkybox);
	
	mCommandList->ExecuteBundle(mBundlesSkybox.Get());

	ID3D12DescriptorHeap* ppHeaps[] = { mSRVHeap.Get(), mSamplerHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

	mCommandList->ExecuteBundle(mBundlesCube.Get());
	
	mCommandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

	mCommandList->ExecuteBundle(mBundlesEarth.Get());

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
