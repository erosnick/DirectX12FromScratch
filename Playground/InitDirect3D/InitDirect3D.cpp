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

using namespace DirectX;
using namespace Microsoft;
using namespace Microsoft::WRL;

//linker
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "d3dcompiler.lib")

struct Vertex
{
	XMFLOAT4 m_vtPos;
	XMFLOAT4 m_vtColor;
};

class InitDirect3DApp : public D3DApp
{
public:
	InitDirect3DApp(HINSTANCE hInstance);
	~InitDirect3DApp();

    virtual bool Initialize() override;

	void createRootSignature();
	void createShaders();
	void createPipelineStateObject();
    void createVertexBuffer();
    void createFence();

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

	ComPtr<ID3D12PipelineState>	mIPipelineState;

	ComPtr<ID3D12Resource> mIVertexBuffer;
	ComPtr<ID3D12Fence> mIFence;

	D3D12_VERTEX_BUFFER_VIEW mVertexBufferView = {};

	float mTrangleSize = 3.0f;

	UINT64 mn64FenceValue = 0ui64;
	HANDLE mHEventFence = nullptr;

	D3D12_RESOURCE_BARRIER stBeginResBarrier = {};
	D3D12_RESOURCE_BARRIER stEndResBarrier = {};
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
		InitDirect3DApp theApp(hInstance);
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

InitDirect3DApp::InitDirect3DApp(HINSTANCE hInstance)
	: D3DApp(hInstance)
{
}

InitDirect3DApp::~InitDirect3DApp()
{
}

bool InitDirect3DApp::Initialize()
{
	if (!D3DApp::Initialize())
		return false;

	createRootSignature();
	createShaders();
	createPipelineStateObject();
	createVertexBuffer();
    createFence();

    return true;
}

void InitDirect3DApp::createRootSignature()
{
	D3D12_ROOT_SIGNATURE_DESC stRootSignatureDesc =
	{
		0
		, nullptr
		, 0
		, nullptr
		, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
	};

	ThrowIfFailed(D3D12SerializeRootSignature(
		&stRootSignatureDesc
		, D3D_ROOT_SIGNATURE_VERSION_1
		, &mISignatureBlob
		, &mIErrorBlob));

	ThrowIfFailed(md3dDevice->CreateRootSignature(0
		, mISignatureBlob->GetBufferPointer()
		, mISignatureBlob->GetBufferSize()
		, IID_PPV_ARGS(&mIRootSignature)));
}

void InitDirect3DApp::createShaders()
{
#if defined(_DEBUG)
	//调试状态下，打开Shader编译的调试标志，不优化
	UINT nCompileFlags =
		D3DCOMPILE_DEBUG
		| D3DCOMPILE_SKIP_OPTIMIZATION;
#else
	UINT nCompileFlags = 0;
#endif

	TCHAR pszAppPath[MAX_PATH] = {};

	if (0 == ::GetModuleFileName(nullptr, pszAppPath, MAX_PATH))
	{
		ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
	}

	WCHAR* lastSlash = _tcsrchr(pszAppPath, _T('\\'));
	if (lastSlash)
	{//删除Exe文件名
		*(lastSlash) = _T('\0');
	}

	lastSlash = _tcsrchr(pszAppPath, _T('\\'));
	if (lastSlash)
	{//删除x64路径
		*(lastSlash) = _T('\0');
	}

	lastSlash = _tcsrchr(pszAppPath, _T('\\'));
	if (lastSlash)
	{//删除Debug 或 Release路径
		*(lastSlash + 1) = _T('\0');
	}

	TCHAR pszShaderFileName[MAX_PATH] = {};

	StringCchPrintf(pszShaderFileName
		, MAX_PATH
		, _T("%sInitDirect3D\\Shader\\shaders.hlsl")
		, pszAppPath);

	ThrowIfFailed(D3DCompileFromFile(pszShaderFileName
		, nullptr
		, nullptr
		, "VSMain"
		, "vs_5_0"
		, nCompileFlags
		, 0
		, &pIBlobVertexShader
		, nullptr));

	ThrowIfFailed(D3DCompileFromFile(pszShaderFileName
		, nullptr
		, nullptr
		, "PSMain"
		, "ps_5_0"
		, nCompileFlags
		, 0
		, &pIBlobPixelShader
		, nullptr));
}

void InitDirect3DApp::createPipelineStateObject()
{
	// Define the vertex input layout.
	D3D12_INPUT_ELEMENT_DESC stInputElementDescs[] =
	{
		{
			"POSITION"
			, 0
			, DXGI_FORMAT_R32G32B32A32_FLOAT
			, 0
			, 0
			, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA
			, 0
		},
		{
			"COLOR"
			, 0
			, DXGI_FORMAT_R32G32B32A32_FLOAT
			, 0
			, 16
			, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA
			, 0
		}
	};

	// 定义渲染管线状态描述结构，创建渲染管线对象
	D3D12_GRAPHICS_PIPELINE_STATE_DESC stPSODesc = {};
	stPSODesc.InputLayout = { stInputElementDescs, _countof(stInputElementDescs) };
	stPSODesc.pRootSignature = mIRootSignature.Get();
	stPSODesc.VS.pShaderBytecode = pIBlobVertexShader->GetBufferPointer();
	stPSODesc.VS.BytecodeLength = pIBlobVertexShader->GetBufferSize();
	stPSODesc.PS.pShaderBytecode = pIBlobPixelShader->GetBufferPointer();
	stPSODesc.PS.BytecodeLength = pIBlobPixelShader->GetBufferSize();

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

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&stPSODesc, IID_PPV_ARGS(&mIPipelineState)));
}


void InitDirect3DApp::createVertexBuffer()
	{
		// 定义三角形的3D数据结构，每个顶点使用三原色之一
		Vertex stTriangleVertices[] =
		{
			{ { 0.0f, 0.25f * mTrangleSize, 0.0f ,1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
			{ { 0.25f * mTrangleSize, -0.25f * mTrangleSize, 0.0f ,1.0f  }, { 0.0f, 1.0f, 0.0f, 1.0f } },
			{ { -0.25f * mTrangleSize, -0.25f * mTrangleSize, 0.0f  ,1.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } }
		};

		const UINT nVertexBufferSize = sizeof(stTriangleVertices);

		D3D12_HEAP_PROPERTIES stHeapProp = { D3D12_HEAP_TYPE_UPLOAD };

		D3D12_RESOURCE_DESC stResSesc = {};
		stResSesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		stResSesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		stResSesc.Flags = D3D12_RESOURCE_FLAG_NONE;
		stResSesc.Format = DXGI_FORMAT_UNKNOWN;
		stResSesc.Width = nVertexBufferSize;
		stResSesc.Height = 1;
		stResSesc.DepthOrArraySize = 1;
		stResSesc.MipLevels = 1;
		stResSesc.SampleDesc.Count = 1;
		stResSesc.SampleDesc.Quality = 0;

		ThrowIfFailed(md3dDevice->CreateCommittedResource(
			&stHeapProp,
			D3D12_HEAP_FLAG_NONE,
			&stResSesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&mIVertexBuffer)));

		UINT8* pVertexDataBegin = nullptr;
		D3D12_RANGE stReadRange = { 0, 0 };

		ThrowIfFailed(mIVertexBuffer->Map(
			0
			, &stReadRange
			, reinterpret_cast<void**>(&pVertexDataBegin)));

		memcpy(pVertexDataBegin
			, stTriangleVertices
			, sizeof(stTriangleVertices));

		mIVertexBuffer->Unmap(0, nullptr);

		mVertexBufferView.BufferLocation = mIVertexBuffer->GetGPUVirtualAddress();
		mVertexBufferView.StrideInBytes = sizeof(Vertex);
		mVertexBufferView.SizeInBytes = nVertexBufferSize;
	}

void InitDirect3DApp::createFence()
{
    // 创建一个同步对象——围栏，用于等待渲染完成，因为现在Draw Call是异步的了
    {
        ThrowIfFailed(md3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mIFence)));
        mn64FenceValue = 1;

        // 创建一个Event同步对象，用于等待围栏事件通知
        mHEventFence = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (mHEventFence == nullptr)
        {
            ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
        }
    }
}

void InitDirect3DApp::OnResize()
{
	D3DApp::OnResize();
}

void InitDirect3DApp::Update(const GameTimer& gt)
{

}

void InitDirect3DApp::Draw(const GameTimer& gt)
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
	mCommandList->SetPipelineState(mIPipelineState.Get());

	// Set the viewport and scissor rect.  This needs to be reset whenever the command list is reset.
	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	// Clear the back buffer and depth buffer.
	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::CornflowerBlue, 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
	mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	mCommandList->IASetVertexBuffers(0, 1, &mVertexBufferView);

	// Specify the buffers we are going to render to.
	mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	mCommandList->DrawInstanced(3, 1, 0, 0);

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
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

	// Wait until frame commands are complete.  This waiting is inefficient and is
	// done for simplicity.  Later we will show how to organize our rendering code
	// so we do not have to wait per frame.
	FlushCommandQueue();
}
