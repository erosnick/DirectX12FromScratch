#include "pch.h"
#include "Chapter3.D3DApp.h"

#include "framework.h"
#include "Resource.h"
#include "pix3.h"
#include "Utils.h"
#include "WICImage.h"
#include <iostream>

D3DApp* D3DApp::app = nullptr;
D3DApp* D3DApp::getApp()
{
	return app;
}

D3DApp::D3DApp()
{
	// Only one D3DApp can be constructed.
	assert(app == nullptr);
	app = this;
}

bool D3DApp::initialize()
{
	if (!initWindow())
	{
		return false;
	}

	try
	{
		initDirect3D();
		loadResources();
		initImGui();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << std::endl;
	}

	return true;
}

int32_t D3DApp::run()
{
	MSG msg{};

	// Main message loop:
	while (true)
	{
		if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
		{
			if (msg.message == WM_QUIT) break;
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
		{
			processInput();
			update();
			render();
		}
	}

	return static_cast<int32_t>(msg.wParam);
}

void createDebugLayer()
{
#if defined(_DEBUG)
	ComPtr<ID3D12Debug> debugController;
	DXCheck(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)), "D3D12GetDebugInterface failed!");
	debugController->EnableDebugLayer();
#endif
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	return D3DApp::getApp()->msgProc(hWnd, message, wParam, lParam);
}

ATOM D3DApp::MyRegisterClass(HINSTANCE hInstance)
{
	WNDCLASSEXW wcex;

	wcex.cbSize = sizeof(WNDCLASSEX);

	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInstance;
	wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_DIRECTX12));
	wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(GetStockObject(NULL_BRUSH));
	wcex.lpszMenuName = nullptr;
	wcex.lpszClassName = szWindowClass;
	wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

	return RegisterClassExW(&wcex);
}

BOOL D3DApp::InitInstance(HINSTANCE hInstance)
{
	RECT rect = { 0, 0, static_cast<long>(windowWidth), static_cast<long>(windowHeight) };
	AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, false);
	int clientWidth = rect.right - rect.left;
	int clientHeight = rect.bottom - rect.top;

	mainWindow = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, clientWidth, clientHeight, nullptr, nullptr, hInstance, nullptr);

	if (!mainWindow)
	{
		return FALSE;
	}

	ShowWindow(mainWindow, SW_SHOW);
	UpdateWindow(mainWindow);

	return TRUE;
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

LRESULT D3DApp::msgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
		return true;

	switch (message)
	{
	case WM_KEYDOWN:
		switch (wParam)
		{
		case VK_UP:
			currentSamplerNo = (currentSamplerNo + 1) % sampleMaxCount;
			break;

		case VK_ESCAPE:
			DestroyWindow(mainWindow);
			break;
		}
		break;
	case WM_CHAR:
		switch (wParam)
		{
		case 'W':
		case 'w':

			break;
		default:
			break;
		}
		break;
	break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

bool D3DApp::initWindow()
{
	wchar_t windowTitle[]{ L"2.Textured Quad" };

	memset(szTitle, 0, MAX_LOADSTRING);
	memcpy_s(szTitle, sizeof(windowTitle), windowTitle, sizeof(windowTitle));

	HINSTANCE appInstance = GetModuleHandle(nullptr);

	MyRegisterClass(appInstance);

	// Perform application initialization:
	if (!InitInstance(appInstance))
	{
		return false;
	}

	return true;
}

void D3DApp::createDebugLayer()
{
#if defined(_DEBUG)
	ComPtr<ID3D12Debug> debugController;
	DXCheck(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)), "D3D12GetDebugInterface failed!");
	debugController->EnableDebugLayer();
#endif
}

void D3DApp::createDXGIFactory()
{
	CreateDXGIFactory2(DXGIFactoryFlags, IID_PPV_ARGS(&DXGIFactory));

	DXCheck(DXGIFactory->MakeWindowAssociation(mainWindow, DXGI_MWA_NO_ALT_ENTER), "IDXGIFactory::MakeWindowAssociation failed!");
}

void D3DApp::createDevice()
{
	for (auto adapterIndex = 0; DXGIFactory->EnumAdapters1(adapterIndex, &DXGIAdapter) != DXGI_ERROR_NOT_FOUND; adapterIndex++)
	{
		DXGI_ADAPTER_DESC1 adapterDesc{};
		DXGIAdapter->GetDesc1(&adapterDesc);

		// �������������������
		if (adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
		{
			continue;
		}

		// �����������D3D֧�ֵļ��ݼ�������ֱ��Ҫ��֧��12.1��������ע�ⷵ�ؽӿڵ��Ǹ���������Ϊ��nullptr������
		// �Ͳ���ʵ�ʴ���һ���豸�ˣ�Ҳ�������ǆ��µ��ٵ���release���ͷŽӿڡ���Ҳ��һ����Ҫ�ļ��ɣ����ס��
		if (SUCCEEDED(D3D12CreateDevice(DXGIAdapter.Get(), D3D_FEATURE_LEVEL_12_1, _uuidof(ID3D12Device), nullptr)))
		{
			break;
		}
	}

	// ����D3D12.1���豸
	DXCheck(D3D12CreateDevice(DXGIAdapter.Get(), D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&device)), "D3D12CreateDevice failed!");
}

void D3DApp::createCommandQueue()
{
	D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
	commandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	DXCheck(device->CreateCommandQueue(&commandQueueDesc,
		IID_PPV_ARGS(&commandQueue)),
		"ID3D12Device::CreateCommandQueue failed!");
}

void D3DApp::createSwapChain()
{
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
	swapChainDesc.BufferCount = frameBackbufferCount;
	swapChainDesc.Width = windowWidth;
	swapChainDesc.Height = windowHeight;
	swapChainDesc.Format = swapChainFormat;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.SampleDesc.Count = 1;

	DXCheck(DXGIFactory->CreateSwapChainForHwnd(commandQueue.Get(),
		mainWindow,
		&swapChainDesc,
		nullptr,
		nullptr, &swapChain1),
		"DXGIFactory::CreateSwapChainForHwnd failed!");

	DXCheck(swapChain1.As(&swapChain), "IDXGISwapChain::As(&swapChain) failed");


	// �õ���ǰ�󻺳�������ţ�Ҳ������һ����Ҫ������ʾ�Ļ����������
	// ע��˴�ʹ���˸߰汾��SwapChain�ӿڵĺ���
	frameIndex = swapChain->GetCurrentBackBufferIndex();
}

void D3DApp::createDescriptorHeap()
{
	D3D12_DESCRIPTOR_HEAP_DESC renderTargetViewDescriptorHeapDesc{};
	renderTargetViewDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	renderTargetViewDescriptorHeapDesc.NumDescriptors = frameBackbufferCount;
	renderTargetViewDescriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	DXCheck(device->CreateDescriptorHeap(&renderTargetViewDescriptorHeapDesc,
		IID_PPV_ARGS(&renderTargetViewDescriptorHeap)),
		"ID3D12Device::CreateDescriptorHeap failed!");

	// �õ�ÿ��������Ԫ�صĴ�С
	renderTargetViewDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	// �������������������������(���ﵥ��ΪImGUI������һ���������ͻ)
	D3D12_DESCRIPTOR_HEAP_DESC shaderResourceViewDescriptorHeapDesc{};
	shaderResourceViewDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	shaderResourceViewDescriptorHeapDesc.NumDescriptors = 2;
	shaderResourceViewDescriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	DXCheck(device->CreateDescriptorHeap(&shaderResourceViewDescriptorHeapDesc, IID_PPV_ARGS(&imGuiShaderResourceViewDescriptorHeap)), "ID3D12Device::CreateDescriptorHeap failed!");
	DXCheck(device->CreateDescriptorHeap(&shaderResourceViewDescriptorHeapDesc, IID_PPV_ARGS(&shaderResourceViewDescriptorHeap)), "ID3D12Device::CreateDescriptorHeap failed!");
}

void D3DApp::createRenderTargetView()
{
	CD3DX12_CPU_DESCRIPTOR_HANDLE renderTargetViewDescriptorHandle(renderTargetViewDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	for (auto i = 0; i < frameBackbufferCount; i++)
	{
		DXCheck(swapChain->GetBuffer(i, IID_PPV_ARGS(&renderTargets[i])), "IDXGISwapChain::GetBuffer failed!");
		device->CreateRenderTargetView(renderTargets[i].Get(), nullptr, renderTargetViewDescriptorHandle);
		renderTargetViewDescriptorHandle.Offset(1, renderTargetViewDescriptorSize);
	}
}

void D3DApp::createSamplerDescriptorHeap()
{
	D3D12_DESCRIPTOR_HEAP_DESC samplerDescriptorHeapDesc{};
	samplerDescriptorHeapDesc.NumDescriptors = sampleMaxCount;
	samplerDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
	samplerDescriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	DXCheck(device->CreateDescriptorHeap(&samplerDescriptorHeapDesc, IID_PPV_ARGS(&samplerDescriptorHeap)), "ID3D12Device::CreateDescriptorHeap failed!");

	samplerDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
}

void D3DApp::createSamplers()
{
	CD3DX12_CPU_DESCRIPTOR_HANDLE samplerDescriptorHeapHandle(samplerDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	D3D12_SAMPLER_DESC samplerDesc{};
	samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	samplerDesc.MinLOD = 0;
	samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
	samplerDesc.MaxAnisotropy = 1;
	samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;

	// Sampler 1
	samplerDesc.BorderColor[0] = 1.0f;
	samplerDesc.BorderColor[1] = 0.0f;
	samplerDesc.BorderColor[2] = 1.0f;
	samplerDesc.BorderColor[3] = 1.0f;
	samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	device->CreateSampler(&samplerDesc, samplerDescriptorHeapHandle);

	samplerDescriptorHeapHandle.Offset(samplerDescriptorSize);

	// Sampler 2
	samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	device->CreateSampler(&samplerDesc, samplerDescriptorHeapHandle);

	samplerDescriptorHeapHandle.Offset(samplerDescriptorSize);

	// Sampler 3
	samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	device->CreateSampler(&samplerDesc, samplerDescriptorHeapHandle);

	samplerDescriptorHeapHandle.Offset(samplerDescriptorSize);

	// Sampler 4
	samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
	samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
	samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
	device->CreateSampler(&samplerDesc, samplerDescriptorHeapHandle);

	samplerDescriptorHeapHandle.Offset(samplerDescriptorSize);

	// Sampler 5
	samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE;
	samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE;
	samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE;
	device->CreateSampler(&samplerDesc, samplerDescriptorHeapHandle);
}

void D3DApp::createRootSignature()
{
	// ����Ƿ�֧��V1.1�汾�ĸ�ǩ��
	D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData;
	featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

	if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
	{
		featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
	}

	// ��GPU��ִ��SetGraphicsRootDescriptorTable�����ǲ��޸������б��е�SRV��������ǿ���ʹ��Ĭ��Rang��Ϊ:
	// D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE
	CD3DX12_DESCRIPTOR_RANGE1 descriptorRanges[2]{};

	descriptorRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE);
	descriptorRanges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0);

	CD3DX12_ROOT_PARAMETER1 rootParameters[2]{};
	rootParameters[0].InitAsDescriptorTable(1, &descriptorRanges[0], D3D12_SHADER_VISIBILITY_PIXEL);
	rootParameters[1].InitAsDescriptorTable(1, &descriptorRanges[1], D3D12_SHADER_VISIBILITY_PIXEL);

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
	rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPtr<ID3DBlob> rootSignatureData;
	ComPtr<ID3DBlob> errorData;

	DXCheck(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &rootSignatureData, &errorData), "D3DX12SerializeVersionedRootSignature failed!");

	DXCheck(device->CreateRootSignature(0,
		rootSignatureData->GetBufferPointer(),
		rootSignatureData->GetBufferSize(),
		IID_PPV_ARGS(&rootSignature)),
		"ID3D12Device::CreateRootSignature failed!");
}

void D3DApp::createGraphicsPipelineState()
{
	ComPtr<ID3DBlob> vertexShader;
	ComPtr<ID3DBlob> pixelShader;

#if defined(_DEBUG)
	uint32_t compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
	uint32_t compileFlags = 0;
#endif

	TCHAR* shaderFileName = L"Assets/Shaders/Texture.hlsl";

	DXCheck(D3DCompileFromFile(shaderFileName, nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, nullptr), "D3DCompileFromFile failed!");
	DXCheck(D3DCompileFromFile(shaderFileName, nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, nullptr), "D3DCompileFromFile failed!");

	D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc{};
	graphicsPipelineStateDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
	graphicsPipelineStateDesc.pRootSignature = rootSignature.Get();
	graphicsPipelineStateDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
	graphicsPipelineStateDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());

	//explicit CD3DX12_RASTERIZER_DESC(CD3DX12_DEFAULT) noexcept
	//{
	//	FillMode = D3D12_FILL_MODE_SOLID;
	//	CullMode = D3D12_CULL_MODE_BACK;
	//	FrontCounterClockwise = FALSE;
	//	DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
	//	DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
	//	SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
	//	DepthClipEnable = TRUE;
	//	MultisampleEnable = FALSE;
	//	AntialiasedLineEnable = FALSE;
	//	ForcedSampleCount = 0;
	//	ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
	//}
	graphicsPipelineStateDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);

	//explicit CD3DX12_BLEND_DESC(CD3DX12_DEFAULT) noexcept
	//{
	//    AlphaToCoverageEnable = FALSE;
	//    IndependentBlendEnable = FALSE;
	//    const D3D12_RENDER_TARGET_BLEND_DESC defaultRenderTargetBlendDesc =
	//    {
	//        FALSE,FALSE,
	//        D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
	//        D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
	//        D3D12_LOGIC_OP_NOOP,
	//        D3D12_COLOR_WRITE_ENABLE_ALL,
	//    };
	//    for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
	//        RenderTarget[i] = defaultRenderTargetBlendDesc;
	//}
	graphicsPipelineStateDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	graphicsPipelineStateDesc.DepthStencilState.DepthEnable = FALSE;
	graphicsPipelineStateDesc.DepthStencilState.StencilEnable = FALSE;
	graphicsPipelineStateDesc.SampleMask = UINT_MAX;
	graphicsPipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	graphicsPipelineStateDesc.NumRenderTargets = 1;
	graphicsPipelineStateDesc.RTVFormats[0] = swapChainFormat;
	graphicsPipelineStateDesc.SampleDesc.Count = 1;

	DXCheck(device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc,
		IID_PPV_ARGS(&graphicsPipelineState)),
		"ID3D12Device::CreateGraphicsPipelineState failed!");
}

void D3DApp::createVertexBuffer()
{
	std::vector<DXVertex> quad =
	{
			{ { -0.25f * aspectRatio, -0.25f * aspectRatio, 0.0f}, { 0.0f, 3.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },	// Bottom left.
			{ { -0.25f * aspectRatio,  0.25f * aspectRatio, 0.0f}, { 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },	// Top left.
			{ {  0.25f * aspectRatio, -0.25f * aspectRatio, 0.0f }, { 3.0f, 3.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } },	// Bottom right.
			{ {  0.25f * aspectRatio,  0.25f * aspectRatio, 0.0f}, { 3.0f, 0.0f }, { 1.0f, 1.0f, 0.0f, 1.0f } }		// Top right.
	};

	vertexCount = static_cast<uint32_t>(quad.size());

	const uint32_t vertexBufferSize = static_cast<uint32_t>(sizeof(DXVertex) * quad.size());

	//explicit CD3DX12_HEAP_PROPERTIES(
	//	D3D12_HEAP_TYPE type,
	//	UINT creationNodeMask = 1,
	//	UINT nodeMask = 1) noexcept
	//{
	//	Type = type;
	//	CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	//	MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	//	CreationNodeMask = creationNodeMask;
	//	VisibleNodeMask = nodeMask;
	//}

	//CD3DX12_RESOURCE_DESC(
	//	D3D12_RESOURCE_DIMENSION dimension,
	//	UINT64 alignment,
	//	UINT64 width,
	//	UINT height,
	//	UINT16 depthOrArraySize,
	//	UINT16 mipLevels,
	//	DXGI_FORMAT format,
	//	UINT sampleCount,
	//	UINT sampleQuality,
	//	D3D12_TEXTURE_LAYOUT layout,
	//	D3D12_RESOURCE_FLAGS flags) noexcept
	//{
	//	Dimension = dimension;
	//	Alignment = alignment;
	//	Width = width;
	//	Height = height;
	//	DepthOrArraySize = depthOrArraySize;
	//	MipLevels = mipLevels;
	//	Format = format;
	//	SampleDesc.Count = sampleCount;
	//	SampleDesc.Quality = sampleQuality;
	//	Layout = layout;
	//	Flags = flags;
	//}

	//static inline CD3DX12_RESOURCE_DESC Buffer(
	//	UINT64 width,
	//	D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE,
	//	UINT64 alignment = 0) noexcept
	//{
	//	return CD3DX12_RESOURCE_DESC(D3D12_RESOURCE_DIMENSION_BUFFER, alignment, width, 1, 1, 1,
	//		DXGI_FORMAT_UNKNOWN, 1, 0, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, flags);
	//}
	DXCheck(device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&vertexBuffer)),
		"ID3D12Device::CreateCommittedResource");

	UINT8* vertexDataBegin = nullptr;

	CD3DX12_RANGE readRange(0, 0);
	DXCheck(vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&vertexDataBegin)), "ID3D12Resource::Map failed!");

	memcpy_s(vertexDataBegin, vertexBufferSize, quad.data(), vertexBufferSize);

	vertexBuffer->Unmap(0, nullptr);

	vertexBufferView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
	vertexBufferView.StrideInBytes = sizeof(DXVertex);
	vertexBufferView.SizeInBytes = vertexBufferSize;
}

void D3DApp::createCommandList()
{
	DXCheck(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(&commandAllocator)),
		"ID3D12Device::CreateCommandAllocator failed!");

	DXCheck(device->CreateCommandList(0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		commandAllocator.Get(),
		graphicsPipelineState.Get(),
		IID_PPV_ARGS(&commandList)), "ID3D12Device::CreateCommandList failed!");
}

void D3DApp::createFence()
{
	DXCheck(device->CreateFence(0,
		D3D12_FENCE_FLAG_NONE,
		IID_PPV_ARGS(&fence)),
		"ID3D12Device::CreateFence failed!");

	fenceValue = 0;

	fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

	if (fenceEvent == nullptr)
	{
		DXCheck(HRESULT_FROM_WIN32(GetLastError()), "CreateEvent failed!");
	}
}

void D3DApp::initDirect3D()
{
	createDebugLayer();
	createDXGIFactory();
	createDevice();
	createCommandQueue();
	createSwapChain();
	createDescriptorHeap();
	createRenderTargetView();
	createSamplerDescriptorHeap();
	createSamplers();
	createRootSignature();
	createGraphicsPipelineState();
	createVertexBuffer();
	createCommandList();
	createFence();
}

void D3DApp::loadResources()
{
	//auto imageData = loadImage(L"Assets/Textures/Kanna.jpg");
	auto imageData = loadImage(L"Assets/Textures/Skrik.bmp");

	// ���������Ĭ�϶�
	D3D12_HEAP_DESC textureHeapDesc{};
	// Ϊ��ָ������ͼƬ����2����С�Ŀռ䣬����û����ϸȡ�����ˣ�ֻ��ָ����һ���㹻��Ŀռ䣬�����������
	// ʵ��Ӧ����Ҳ��Ҫ�ۺϿ��Ƿ���ѵĴ�С���Ա�������ö�
	textureHeapDesc.SizeInBytes = ROUND_UP(2 * imageData.rowPitch * imageData.width, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
	// ָ���ѵĶ��뷽ʽ������ʹ����Ĭ�ϵ�64K�߽���룬��Ϊ������ʱ����ҪMSAA֧��
	textureHeapDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
	textureHeapDesc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
	textureHeapDesc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	textureHeapDesc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	// �ܾ���ȾĿ�������ܾ����ģ������ʵ�ʾ�ֻ�������ڷ���ͨ����
	textureHeapDesc.Flags = D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES | D3D12_HEAP_FLAG_DENY_BUFFERS;

	DXCheck(device->CreateHeap(&textureHeapDesc, IID_PPV_ARGS(&textureHeap)), "ID3D12Device::CreateHealp failed!");

	// ����2D����
	D3D12_RESOURCE_DESC textureDesc{};
	textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	textureDesc.MipLevels = 1;
	textureDesc.Format = imageData.format;
	textureDesc.Width = imageData.width;
	textureDesc.Height = imageData.height;
	textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	textureDesc.DepthOrArraySize = 1;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;

	// ʹ�á���λ��ʽ������������ע��������������ڲ�ʵ���Ѿ�û�д洢������ͷŵ�ʵ�ʲ����ˣ��������ܸܺ�
	// ͬʱ������������Ϸ�������CreatePlacedResource��������ͬ��������Ȼǰ�������ǲ��ڱ�ʹ�õ�ʱ�򣬲ſ������ö�
	DXCheck(device->CreatePlacedResource(textureHeap.Get(),
											   0,
											   &textureDesc,	// ����ʹ��CD3DX12_RESOURCE_DESC::Tex2D���򻯽ṹ��ĳ�ʼ��
												D3D12_RESOURCE_STATE_COPY_DEST,
												nullptr,
												IID_PPV_ARGS(&texture)), "ID3D12Device::CreatePlacedResource failed!");

	// ��ȡ�ϴ�����Դ����Ĵ�С������ߴ�ͨ������ʵ��ͼƬ�ĳߴ�
	const uint64_t uploadBufferSize = GetRequiredIntermediateSize(texture.Get(), 0, 1);

	// �����ϴ���
	D3D12_HEAP_DESC uploadHeapDesc{};
	// �ߴ���Ȼ��ʵ���������ݴ�С��2������64K�߽�����С
	uploadHeapDesc.SizeInBytes = ROUND_UP(2 * uploadBufferSize, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

	// ע���ϴ��ѿ϶���Buffer���ͣ����Բ�ָ�����뷽ʽ����Ĭ����64k�߽����
	uploadHeapDesc.Alignment = 0;
	uploadHeapDesc.Properties.Type = D3D12_HEAP_TYPE_UPLOAD;
	uploadHeapDesc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	uploadHeapDesc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

	// �ϴ��Ѿ��ǻ��壬���԰ڷ���������
	uploadHeapDesc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;

	DXCheck(device->CreateHeap(&uploadHeapDesc, IID_PPV_ARGS(&uploadHeap)), "ID3D12Device::CreateHealp failed!");

	// ʹ�á���λ��ʽ�����������ϴ��������ݵĻ�����Դ

	// ���������ϴ��������Դ, ע����������Buffer
	// �ϴ��Ѷ���GPU������˵�����Ǻܲ�ģ�
	// ���Զ��ڼ������������������������
	// ͨ�������ϴ���GPU���ʸ���Ч��Ĭ�϶���
	ComPtr<ID3D12Resource> textureUpload;

	DXCheck(device->CreatePlacedResource(uploadHeap.Get(),
								     0,
									     &CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
							        D3D12_RESOURCE_STATE_GENERIC_READ,
							  nullptr,
									      IID_PPV_ARGS(&textureUpload)), "ID3D12Device::CreatePlacedResource failed!");

	// ������Դ�����С������ʵ��ͼƬ���ݴ洢���ڴ��С
	void* textureData = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, uploadBufferSize);

	if (textureData == nullptr)
	{
		DXThrow("HeapAlloc failed!");
	}

	// ��ͼƬ�ж�ȡ����
	DXCheck(imageData.data->CopyPixels(
		nullptr,
		imageData.rowPitch,
		//ע���������ͼƬ������ʵ�Ĵ�С�����ֵͨ��С�ڻ���Ĵ�С
		static_cast<uint32_t>(imageData.rowPitch * imageData.width),
		reinterpret_cast<uint8_t*>(textureData)), "IWICBitmapSource::CopyPixels failed!");

	//{//������δ�������DX12��ʾ����ֱ��ͨ����仺�������һ���ڰ׷��������
	// //��ԭ��δ��룬Ȼ��ע�������CopyPixels���ÿ��Կ����ڰ׷��������Ч��
	//	const UINT rowPitch = nPicRowPitch; //nTextureW * 4; //static_cast<UINT>(n64UploadBufferSize / nTextureH);
	//	const UINT cellPitch = rowPitch >> 3;		// The width of a cell in the checkboard texture.
	//	const UINT cellHeight = nTextureW >> 3;	// The height of a cell in the checkerboard texture.
	//	const UINT textureSize = static_cast<UINT>(n64UploadBufferSize);
	//	UINT nTexturePixelSize = static_cast<UINT>(n64UploadBufferSize / nTextureH / nTextureW);

	//	UINT8* pData = reinterpret_cast<UINT8*>(pbPicData);

	//	for (UINT n = 0; n < textureSize; n += nTexturePixelSize)
	//	{
	//		UINT x = n % rowPitch;
	//		UINT y = n / rowPitch;
	//		UINT i = x / cellPitch;
	//		UINT j = y / cellHeight;

	//		if (i % 2 == j % 2)
	//		{
	//			pData[n] = 0x00;		// R
	//			pData[n + 1] = 0x00;	// G
	//			pData[n + 2] = 0x00;	// B
	//			pData[n + 3] = 0xff;	// A
	//		}
	//		else
	//		{
	//			pData[n] = 0xff;		// R
	//			pData[n + 1] = 0xff;	// G
	//			pData[n + 2] = 0xff;	// B
	//			pData[n + 3] = 0xff;	// A
	//		}
	//	}
	//}

	// ��ȡ���ϴ��ѿ����������ݵ�һЩ����ת���ߴ���Ϣ
	// ���ڸ��ӵ�DDS�������Ƿǳ���Ҫ�Ĺ���
	uint64_t requiredSize = 0;
	uint32_t numSubresources = 1;
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT textureLayout{};
	uint64_t textureRowSize = 0;
	uint32_t textureRowNum = 0;

	D3D12_RESOURCE_DESC resourceDesc = texture->GetDesc();

	device->GetCopyableFootprints(
		&resourceDesc,
		0,
		numSubresources,
		0,
		&textureLayout,
		&textureRowNum,
		&textureRowSize,
		&requiredSize);

	// ��Ϊ�ϴ���ʵ�ʾ���CPU�������ݵ�GPU���н�
	// �������ǿ���ʹ����Ϥ��Map����������ӳ�䵽CPU�ڴ��ַ��
	// Ȼ�����ǰ��н����ݸ��Ƶ��ϴ�����
	// ��Ҫע�����֮���԰��п�������ΪGPU��Դ���д�С
	// ��ʵ��ͼƬ���д�С���в����,���ߵ��ڴ�߽����Ҫ���ǲ�һ����
	uint8_t* data = nullptr;

	DXCheck(textureUpload->Map(0, nullptr, reinterpret_cast<void**>(&data)), "ID3D12Resource::Map failed!");

	uint8_t* destSlice = reinterpret_cast<uint8_t*>(data) + textureLayout.Offset;
	const uint8_t* srcSlice = reinterpret_cast<const uint8_t*>(textureData);

	for (uint32_t y = 0; y < textureRowNum; y++)
	{
		auto dstWriteOffset = static_cast<size_t>(textureLayout.Footprint.RowPitch) * y;
		auto srcCopyOffset = static_cast<size_t>(imageData.rowPitch) * y;
		memcpy_s(destSlice + dstWriteOffset, imageData.rowPitch, srcSlice + srcCopyOffset, imageData.rowPitch);
	}

	// ȡ��ӳ�� �����ױ��������ÿ֡�ı任��������ݣ�������������Unmap�ˣ�
	// ������פ�ڴ�,������������ܣ���Ϊÿ��Map��Unmap�Ǻܺ�ʱ�Ĳ���
	// ��Ϊ�������붼��64λϵͳ��Ӧ���ˣ���ַ�ռ����㹻�ģ�������ռ�ò���Ӱ��ʲô
	textureUpload->Unmap(0, nullptr);

	// �ͷ�ͼƬ���ݣ���һ���ɾ��ĳ���Ա
	HeapFree(GetProcessHeap(), 0, textureData);

	// ��������з������ϴ��Ѹ����������ݵ�Ĭ�϶ѵ�����
	CD3DX12_TEXTURE_COPY_LOCATION dest(texture.Get(), 0);
	CD3DX12_TEXTURE_COPY_LOCATION src(textureUpload.Get(), textureLayout);

	commandList->CopyTextureRegion(&dest, 0, 0, 0, &src, nullptr);

	// ����һ����Դ���ϣ�ͬ����ȷ�ϸ��Ʋ������
	// ֱ��ʹ�ýṹ��Ȼ����õ���ʽ
	D3D12_RESOURCE_BARRIER resourceBarrier{};
	resourceBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	resourceBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	resourceBarrier.Transition.pResource = texture.Get();
	resourceBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	resourceBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	resourceBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

	commandList->ResourceBarrier(1, &resourceBarrier);

	// ����ʹ��D3DX12���еĹ�������õĵȼ���ʽ������ķ�ʽ�����һЩ
	// pICommandList->ResourceBarrier(1
	//	 , &CD3DX12_RESOURCE_BARRIER::Transition(pITexcute.Get()
	//	 , D3D12_RESOURCE_STATE_COPY_DEST
	//	 , D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
	// );

	// ���մ���SRV������
	D3D12_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc{};
	shaderResourceViewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	shaderResourceViewDesc.Format = textureDesc.Format;
	shaderResourceViewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	shaderResourceViewDesc.Texture2D.MipLevels = 1;

	// �����������д���SRV������
	device->CreateShaderResourceView(texture.Get(), &shaderResourceViewDesc, shaderResourceViewDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	// ִ�������б��ȴ�������Դ�ϴ���ɣ���һ���Ǳ����
	DXCheck(commandList->Close(), "ID3D12GraphicsCommandList::Close failed!");
	ID3D12CommandList* commandLists[] = { commandList.Get() };
	commandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);

	// Fence��ʼֵҪ����0����������if (fence->GetCompletedValue() < currentFenceValue)��ԶΪfalse
	fenceValue = 1;

	const uint64_t currentFenceValue = fenceValue;

	DXCheck(commandQueue->Signal(fence.Get(), currentFenceValue), "ID3D12CommandQueue::Signal failed!");
	fenceValue++;

	// ��������û������ִ�е�Χ����ǵ����û�о������¼�ȥ�ȴ���ע��ʹ�õ���������ж����ָ��
	if (fence->GetCompletedValue() < currentFenceValue)
	{
		DXCheck(fence->SetEventOnCompletion(currentFenceValue, fenceEvent), "ID3D12Fence::SetEventOnCompletion failed!");
		WaitForSingleObject(fenceEvent, INFINITE);
	}

	//�����������Resetһ�£��ղ��Ѿ�ִ�й���һ���������������
	DXCheck(commandAllocator->Reset(), "ID3D12CommandAllocator::Reset failed!");

	//Reset�����б�������ָ�������������PSO����
	DXCheck(commandList->Reset(commandAllocator.Get(), graphicsPipelineState.Get()), "ID3D12GraphicsCommandList::Reset failed!");
}

void D3DApp::initImGui()
{
	ImGui_ImplWin32_EnableDpiAwareness();

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
	//ImGui::StyleColorsLight();

	// Setup Platform/Renderer backends
	ImGui_ImplWin32_Init(mainWindow);
	ImGui_ImplDX12_Init(device.Get(), frameBackbufferCount,
		DXGI_FORMAT_R8G8B8A8_UNORM, imGuiShaderResourceViewDescriptorHeap.Get(),
		imGuiShaderResourceViewDescriptorHeap.Get()->GetCPUDescriptorHandleForHeapStart(),
		imGuiShaderResourceViewDescriptorHeap.Get()->GetGPUDescriptorHandleForHeapStart());

	// Load Fonts
	// - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
	// - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
	// - If the file cannot be loaded, the function will return NULL. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
	// - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
	// - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use Freetype for higher quality font rendering.
	// - Read 'docs/FONTS.md' for more instructions and details.
	// - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
	//io.Fonts->AddFontDefault();
	//io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf", 18.0f);
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
	//io.Fonts->AddFontFromFileTTF("Assets/fonts/Roboto-Medium.ttf", 24.0f);
	io.Fonts->AddFontFromFileTTF("Assets/fonts/ProggyClean.ttf", 20.0f);
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
	//ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, NULL, io.Fonts->GetGlyphRangesJapanese());
	//IM_ASSERT(font != NULL);
}

void D3DApp::updateImGui()
{
	// Start the Dear ImGui frame
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
}

void D3DApp::createImGuiWidgets()
{
	// 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
	if (showDemoWindow)
		ImGui::ShowDemoWindow(&showDemoWindow);

	// 2. Show a simple window that we create ourselves. We use a Begin/End pair to create a named window.
	{
		static float f = 0.0f;
		static int counter = 0;

		ImGui::Begin("Hello, world!");                          // Create a window called "Hello, world!" and append into it.

		ImGui::Text("This is some useful text.");               // Display some text (you can use a format strings too)
		ImGui::Checkbox("Demo Window", &showDemoWindow);      // Edit bools storing our window open/close state
		ImGui::Checkbox("Another Window", &showAnotherWindow);

		ImGui::SliderFloat("float", &f, 0.0f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f
		ImGui::ColorEdit3("clear color", (float*)&clearColor); // Edit 3 floats representing a color

		if (ImGui::Button("Button"))                            // Buttons return true when clicked (most widgets return true when edited/activated)
			counter++;
		ImGui::SameLine();
		ImGui::Text("counter = %d", counter);

		ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
		ImGui::End();
	}

	// 3. Show another simple window.
	if (showAnotherWindow)
	{
		ImGui::Begin("Another Window", &showAnotherWindow);   // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
		ImGui::Text("Hello from another window!");
		if (ImGui::Button("Close Me"))
			showAnotherWindow = false;
		ImGui::End();
	}
}

void D3DApp::renderImGui()
{
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList.Get());
}

void D3DApp::shutdownImGui()
{
	// Cleanup
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

void D3DApp::update()
{
	updateImGui();
	createImGuiWidgets();
}

void D3DApp::processInput()
{
	if (GetAsyncKeyState('W') & 0x8000)
	{
	}

	if (GetAsyncKeyState('S') & 0x8000)
	{
	}

	if (GetAsyncKeyState('A') & 0x8000)
	{
	}

	if (GetAsyncKeyState('D') & 0x8000)
	{
	}
}

void D3DApp::render()
{
	commandList->RSSetViewports(1, &viewport);
	commandList->RSSetScissorRects(1, &scissorRect);

	//static inline CD3DX12_RESOURCE_BARRIER Transition(
	//	_In_ ID3D12Resource * pResource,
	//	D3D12_RESOURCE_STATES stateBefore,
	//	D3D12_RESOURCE_STATES stateAfter,
	//	UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
	//	D3D12_RESOURCE_BARRIER_FLAGS flags = D3D12_RESOURCE_BARRIER_FLAG_NONE) noexcept
	//{
	//	CD3DX12_RESOURCE_BARRIER result = {};
	//	D3D12_RESOURCE_BARRIER& barrier = result;
	//	result.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	//	result.Flags = flags;
	//	barrier.Transition.pResource = pResource;
	//	barrier.Transition.StateBefore = stateBefore;
	//	barrier.Transition.StateAfter = stateAfter;
	//	barrier.Transition.Subresource = subresource;
	//	return result;
	//}
	commandList->ResourceBarrier(1,
		&CD3DX12_RESOURCE_BARRIER::Transition(renderTargets[frameIndex].Get(),
			D3D12_RESOURCE_STATE_PRESENT,
			D3D12_RESOURCE_STATE_RENDER_TARGET));

	CD3DX12_CPU_DESCRIPTOR_HANDLE renderTargetViewHandle(renderTargetViewDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		frameIndex, renderTargetViewDescriptorSize);

	commandList->OMSetRenderTargets(1, &renderTargetViewHandle, FALSE, nullptr);

	commandList->SetGraphicsRootSignature(rootSignature.Get());

	ID3D12DescriptorHeap* descriptorHeaps[] = { shaderResourceViewDescriptorHeap.Get(), samplerDescriptorHeap.Get() };
	commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	commandList->SetGraphicsRootDescriptorTable(0, shaderResourceViewDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

	CD3DX12_GPU_DESCRIPTOR_HANDLE samplerDescriptorHandle(samplerDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), currentSamplerNo, samplerDescriptorSize);

	commandList->SetGraphicsRootDescriptorTable(1, samplerDescriptorHandle);

	const float clearColor[]{ 0.4f, 0.6f, 0.9f, 1.0f };
	commandList->ClearRenderTargetView(renderTargetViewHandle, clearColor, 0, nullptr);
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	commandList->IASetVertexBuffers(0, 1, &vertexBufferView);

	commandList->DrawInstanced(vertexCount, 1, 0, 0);

	ID3D12DescriptorHeap* imGuiDescriptorHeaps[] = { imGuiShaderResourceViewDescriptorHeap.Get() };
	commandList->SetDescriptorHeaps(1, imGuiDescriptorHeaps);

	// Rendering
	ImGui::Render();
	renderImGui();

	commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(renderTargets[frameIndex].Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	DXCheck(commandList->Close(), "ID3D12GraphicsCommandList::Close failed!");

	ID3D12CommandList* commandLists[]{ commandList.Get() };

	commandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);

	DXCheck(swapChain->Present(1, 0), "IDXGISwapChain::Present failed!");

	//��ʼͬ��GPU��CPU��ִ�У��ȼ�¼Χ�����ֵ
	const UINT64 targetFenceValue = fenceValue;

	DXCheck(commandQueue->Signal(fence.Get(), targetFenceValue), "ID3D12CommandQueue::Signal failed!");
	fenceValue++;

	// ��������û������ִ�е�Χ����ǵ����û�о������¼�ȥ�ȴ���ע��ʹ�õ���������ж����ָ��
	if (fence->GetCompletedValue() < targetFenceValue)
	{
		DXCheck(fence->SetEventOnCompletion(targetFenceValue, fenceEvent), "ID3D12Fence::SetEventOnCompletion failed!");
		WaitForSingleObject(fenceEvent, INFINITE);
	}

	// ������˵��һ���������������ִ�����ˣ�������ʹ������ǵ�һ֡�Ѿ���Ⱦ���ˣ�����׼��ִ����һ֡��Ⱦ
	// ��ȡ�µĺ󻺳���ţ���ΪPresent�������ʱ�󻺳����ž͸�����
	frameIndex = swapChain->GetCurrentBackBufferIndex();

	// �����������Resetһ��
	DXCheck(commandAllocator->Reset(), "ID3D12CommandAllocator::Reset failed!");

	// Reset�����б�������ָ�������������PSO����
	DXCheck(commandList->Reset(commandAllocator.Get(), graphicsPipelineState.Get()), "ID3D12GraphicsCommandList::Reset failed!");
}

