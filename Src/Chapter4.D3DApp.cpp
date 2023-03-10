#include "pch.h"
#include "4.D3DApp.h"

#include "framework.h"
#include "Resource.h"
#include "pix3.h"
#include "Utils.h"
#include "WICImage.h"
#include <iostream>
#include <fmt/format.h>

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
		initImGui();
	}
	catch (const std::exception& e)
	{
		fmt::print("{}\n", e.what());
	}

	return true;
}

int32_t D3DApp::run()
{
	MSG msg{};

	// 记录帧开始时间，和当前时间，以循环结束为界
	static auto startTimeDuration = std::chrono::high_resolution_clock::now().time_since_epoch();
	static auto simulationTime = std::chrono::duration<float, std::chrono::seconds::period>(startTimeDuration).count();
	startTime = simulationTime;

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
			auto currentTime = std::chrono::high_resolution_clock::now().time_since_epoch();
			auto realTime = std::chrono::duration<float, std::chrono::seconds::period>(currentTime).count();

			while (simulationTime < realTime)
			{
				simulationTime += FrameTime;
				processInput(FrameTime);
				update();
			}

			updateFPSCounter();
			updateImGui();
			createImGuiWidgets();
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

	ShowWindow(mainWindow, SW_SHOWDEFAULT);
	UpdateWindow(mainWindow);

	return TRUE;
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

LRESULT D3DApp::msgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
	{
		return true;
	}

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
	break;
	case WM_MOUSEMOVE:
		break;
	case WM_MOUSEHOVER:
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
	wchar_t windowTitle[]{ L"4.Textured Cube" };
	wchar_t windowClass[]{ L"DirectX12Window" };

	memset(szTitle, 0, MAX_LOADSTRING);
	memcpy_s(szTitle, sizeof(windowTitle), windowTitle, sizeof(windowTitle));

	memset(szWindowClass, 0, MAX_LOADSTRING);
	memcpy_s(szWindowClass, sizeof(windowClass), windowClass, sizeof(windowClass));

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

		// 软件虚拟适配器，跳过
		if (adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
		{
			continue;
		}

		// 检查适配器对D3D支持的兼容级别，这里直接要求支持12.1的能力，注意返回接口的那个参数被置为了nullptr，这样
		// 就不会实际创建一个设备了，也不用我们啰嗦的再调用release来释放接口。这也是一个重要的技巧，请记住！
		if (SUCCEEDED(D3D12CreateDevice(DXGIAdapter.Get(), D3D_FEATURE_LEVEL_12_1, _uuidof(ID3D12Device), nullptr)))
		{
			break;
		}
	}

	// 创建D3D12.1的设备
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
	//swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

	DXCheck(DXGIFactory->CreateSwapChainForHwnd(commandQueue.Get(),
		mainWindow,
		&swapChainDesc,
		nullptr,
		nullptr, &swapChain1),
		"DXGIFactory::CreateSwapChainForHwnd failed!");

	DXCheck(swapChain1.As(&swapChain), "IDXGISwapChain::As(&swapChain) failed");

	// 得到当前后缓冲区的序号，也就是下一个将要呈送显示的缓冲区的序号
	// 注意此处使用了高版本的SwapChain接口的函数
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

	// 得到每个描述符元素的大小
	renderTargetViewDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	// 创建用于纹理采样的描述符堆(这里单独为ImGUI创建了一个，避免冲突)
	D3D12_DESCRIPTOR_HEAP_DESC shaderResourceViewDescriptorHeapDesc{};
	shaderResourceViewDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	shaderResourceViewDescriptorHeapDesc.NumDescriptors = 1;
	shaderResourceViewDescriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	DXCheck(device->CreateDescriptorHeap(&shaderResourceViewDescriptorHeapDesc, IID_PPV_ARGS(&imGuiShaderResourceViewDescriptorHeap)), "ID3D12Device::CreateDescriptorHeap failed!");

	shaderResourceViewDescriptorHeapDesc.NumDescriptors = 3; // 1 SRV + 1 CBV + 1 DSV

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

void D3DApp::createRootSignature()
{
	// 检测是否支持V1.1版本的根签名
	D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData;
	featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

	if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
	{
		featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
	}

	// 在GPU上执行SetGraphicsRootDescriptorTable后，我们不修改命令列表中的SRV，因此我们可以使用默认Rang行为:
	// D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE
	CD3DX12_DESCRIPTOR_RANGE1 descriptorRanges[3]{};

	descriptorRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE);
	descriptorRanges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
	descriptorRanges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0);

	CD3DX12_ROOT_PARAMETER1 rootParameters[3]{};
	rootParameters[0].InitAsDescriptorTable(1, &descriptorRanges[0], D3D12_SHADER_VISIBILITY_PIXEL);		// SRV仅 Pixel Shader 可见
	rootParameters[1].InitAsDescriptorTable(1, &descriptorRanges[1], D3D12_SHADER_VISIBILITY_ALL);		// CBV是所有 Shader 可见
	rootParameters[2].InitAsDescriptorTable(1, &descriptorRanges[2], D3D12_SHADER_VISIBILITY_PIXEL);		// Sampler 仅 Pixel Shader 可见

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

	TCHAR* shaderFileName = L"Assets/Shaders/4.Texture.hlsl";

	DXCheck(D3DCompileFromFile(shaderFileName, nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, nullptr), "D3DCompileFromFile failed!");
	DXCheck(D3DCompileFromFile(shaderFileName, nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, nullptr), "D3DCompileFromFile failed!");

	D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
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
	graphicsPipelineStateDesc.RasterizerState.FrontCounterClockwise = TRUE;

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
	graphicsPipelineStateDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	graphicsPipelineStateDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	graphicsPipelineStateDesc.SampleMask = UINT_MAX;
	graphicsPipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	graphicsPipelineStateDesc.NumRenderTargets = 1;
	graphicsPipelineStateDesc.RTVFormats[0] = swapChainFormat;
	graphicsPipelineStateDesc.SampleDesc.Count = 1;

	DXCheck(device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc,
		IID_PPV_ARGS(&graphicsPipelineState)),
		"ID3D12Device::CreateGraphicsPipelineState failed!");
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

void D3DApp::createUploadHeap(uint64_t heapSize)
{
	// 创建上传堆
	D3D12_HEAP_DESC uploadHeapDesc{};

	// 尺寸依然是实际纹理数据大小的2倍并且64K边界对齐大小
	uploadHeapDesc.SizeInBytes = heapSize;

	// 注意上传堆肯定是Buffer类型，可以不指定对齐方式，其默认是64k边界对齐
	uploadHeapDesc.Alignment = 0;
	uploadHeapDesc.Properties.Type = D3D12_HEAP_TYPE_UPLOAD;
	uploadHeapDesc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	uploadHeapDesc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

	// 上传堆就是缓冲，可以摆放任意数据
	uploadHeapDesc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;

	DXCheck(device->CreateHeap(&uploadHeapDesc, IID_PPV_ARGS(&uploadHeap)), "ID3D12Device::CreateHealp failed!");
}

void D3DApp::loadCubeResource()
{
	std::vector<DXVertex> cubeVertices =
	{
		// Front
		{ {-1.0f * boxSize,  1.0f * boxSize, -1.0f * boxSize}, {0.0f,  0.0f, -1.0f}, {0.0f * texcoordScale, 0.0f * texcoordScale}, { 1.0f, 1.0f, 1.0f, 1.0f } },
		{ {1.0f * boxSize,  1.0f * boxSize, -1.0f * boxSize},  {0.0f,  0.0f, -1.0f}, {1.0f * texcoordScale, 0.0f * texcoordScale}, { 1.0f, 1.0f, 1.0f, 1.0f } },
		{ {-1.0f * boxSize, -1.0f * boxSize, -1.0f * boxSize}, {0.0f,  0.0f, -1.0f}, {0.0f * texcoordScale, 1.0f * texcoordScale}, { 1.0f, 1.0f, 1.0f, 1.0f } },
		{ {-1.0f * boxSize, -1.0f * boxSize, -1.0f * boxSize}, {0.0f,  0.0f, -1.0f}, {0.0f * texcoordScale, 1.0f * texcoordScale}, { 1.0f, 1.0f, 1.0f, 1.0f } },
		{ {1.0f * boxSize,  1.0f * boxSize, -1.0f * boxSize},  {0.0f, 0.0f,  -1.0f}, {1.0f * texcoordScale, 0.0f * texcoordScale}, { 1.0f, 1.0f, 1.0f, 1.0f } },
		{ {1.0f * boxSize, -1.0f * boxSize, -1.0f * boxSize},  {0.0f,  0.0f, -1.0f}, {1.0f * texcoordScale, 1.0f * texcoordScale}, { 1.0f, 1.0f, 1.0f, 1.0f } },

		// Right
		{ {1.0f * boxSize,  1.0f * boxSize, -1.0f * boxSize},  {1.0f,  0.0f,  0.0f}, {0.0f * texcoordScale, 0.0f * texcoordScale}, { 1.0f, 1.0f, 1.0f, 1.0f } },
		{ {1.0f * boxSize,  1.0f * boxSize,  1.0f * boxSize},  {1.0f,  0.0f,  0.0f}, {1.0f * texcoordScale, 0.0f * texcoordScale}, { 1.0f, 1.0f, 1.0f, 1.0f } },
		{ {1.0f * boxSize, -1.0f * boxSize, -1.0f * boxSize},  {1.0f,  0.0f,  0.0f}, {0.0f * texcoordScale, 1.0f * texcoordScale}, { 1.0f, 1.0f, 1.0f, 1.0f } },
		{ {1.0f * boxSize, -1.0f * boxSize, -1.0f * boxSize},  {1.0f,  0.0f,  0.0f}, {0.0f * texcoordScale, 1.0f * texcoordScale}, { 1.0f, 1.0f, 1.0f, 1.0f } },
		{ {1.0f * boxSize,  1.0f * boxSize,  1.0f * boxSize},  {1.0f,  0.0f,  0.0f}, {1.0f * texcoordScale, 0.0f * texcoordScale}, { 1.0f, 1.0f, 1.0f, 1.0f } },
		{ {1.0f * boxSize, -1.0f * boxSize,  1.0f * boxSize},  {1.0f,  0.0f,  0.0f}, {1.0f * texcoordScale, 1.0f * texcoordScale}, { 1.0f, 1.0f, 1.0f, 1.0f } },

		// Back
		{ {1.0f * boxSize,  1.0f * boxSize,  1.0f * boxSize},  {0.0f,  0.0f,  1.0f}, {0.0f * texcoordScale, 0.0f * texcoordScale}, { 1.0f, 1.0f, 1.0f, 1.0f } },
		{ {-1.0f * boxSize,  1.0f * boxSize,  1.0f * boxSize}, {0.0f,  0.0f,  1.0f}, {1.0f * texcoordScale, 0.0f * texcoordScale}, { 1.0f, 1.0f, 1.0f, 1.0f } },
		{ {1.0f * boxSize, -1.0f * boxSize,  1.0f * boxSize},  {0.0f,  0.0f,  1.0f}, {0.0f * texcoordScale, 1.0f * texcoordScale}, { 1.0f, 1.0f, 1.0f, 1.0f } },
		{ {1.0f * boxSize, -1.0f * boxSize,  1.0f * boxSize},  {0.0f,  0.0f,  1.0f}, {0.0f * texcoordScale, 1.0f * texcoordScale}, { 1.0f, 1.0f, 1.0f, 1.0f } },
		{ {-1.0f * boxSize,  1.0f * boxSize,  1.0f * boxSize}, {0.0f,  0.0f,  1.0f}, {1.0f * texcoordScale, 0.0f * texcoordScale}, { 1.0f, 1.0f, 1.0f, 1.0f } },
		{ {-1.0f * boxSize, -1.0f * boxSize,  1.0f * boxSize}, {0.0f,  0.0f,  1.0f}, {1.0f * texcoordScale, 1.0f * texcoordScale}, { 1.0f, 1.0f, 1.0f, 1.0f } },

		// Left
		{ {-1.0f * boxSize,  1.0f * boxSize,  1.0f * boxSize}, {-1.0f,  0.0f,  0.0f}, {0.0f * texcoordScale, 0.0f * texcoordScale}, { 1.0f, 1.0f, 1.0f, 1.0f } },
		{ {-1.0f * boxSize,  1.0f * boxSize, -1.0f * boxSize}, {-1.0f,  0.0f,  0.0f}, {1.0f * texcoordScale, 0.0f * texcoordScale}, { 1.0f, 1.0f, 1.0f, 1.0f } },
		{ {-1.0f * boxSize, -1.0f * boxSize,  1.0f * boxSize}, {-1.0f,  0.0f,  0.0f}, {0.0f * texcoordScale, 1.0f * texcoordScale}, { 1.0f, 1.0f, 1.0f, 1.0f } },
		{ {-1.0f * boxSize, -1.0f * boxSize,  1.0f * boxSize}, {-1.0f,  0.0f,  0.0f}, {0.0f * texcoordScale, 1.0f * texcoordScale}, { 1.0f, 1.0f, 1.0f, 1.0f } },
		{ {-1.0f * boxSize,  1.0f * boxSize, -1.0f * boxSize}, {-1.0f,  0.0f,  0.0f}, {1.0f * texcoordScale, 0.0f * texcoordScale}, { 1.0f, 1.0f, 1.0f, 1.0f } },
		{ {-1.0f * boxSize, -1.0f * boxSize, -1.0f * boxSize}, {-1.0f,  0.0f,  0.0f}, {1.0f * texcoordScale, 1.0f * texcoordScale}, { 1.0f, 1.0f, 1.0f, 1.0f } },

		// Top
		{ {-1.0f * boxSize,  1.0f * boxSize,  1.0f * boxSize}, {0.0f,  1.0f,  0.0f}, {0.0f * texcoordScale, 0.0f * texcoordScale}, { 1.0f, 1.0f, 1.0f, 1.0f } },
		{ {1.0f * boxSize,  1.0f * boxSize,  1.0f * boxSize},  {0.0f,  1.0f,  0.0f}, {1.0f * texcoordScale, 0.0f * texcoordScale}, { 1.0f, 1.0f, 1.0f, 1.0f } },
		{ {-1.0f * boxSize,  1.0f * boxSize, -1.0f * boxSize}, {0.0f,  1.0f,  0.0f}, {0.0f * texcoordScale, 1.0f * texcoordScale}, { 1.0f, 1.0f, 1.0f, 1.0f } },
		{ {-1.0f * boxSize,  1.0f * boxSize, -1.0f * boxSize}, {0.0f,  1.0f,  0.0f}, {0.0f * texcoordScale, 1.0f * texcoordScale}, { 1.0f, 1.0f, 1.0f, 1.0f } },
		{ {1.0f * boxSize,  1.0f * boxSize,  1.0f * boxSize},  {0.0f,  1.0f,  0.0f}, {1.0f * texcoordScale, 0.0f * texcoordScale}, { 1.0f, 1.0f, 1.0f, 1.0f } },
		{ {1.0f * boxSize,  1.0f * boxSize, -1.0f * boxSize},  {0.0f,  1.0f,  0.0f}, {1.0f * texcoordScale, 1.0f * texcoordScale}, { 1.0f, 1.0f, 1.0f, 1.0f } },

		// Bottom
		{ {-1.0f * boxSize, -1.0f * boxSize, -1.0f * boxSize}, {0.0f, -1.0f,  0.0f}, {0.0f * texcoordScale, 0.0f * texcoordScale}, { 1.0f, 1.0f, 1.0f, 1.0f } },
		{ {1.0f * boxSize, -1.0f * boxSize, -1.0f * boxSize},  {0.0f, -1.0f,  0.0f}, {1.0f * texcoordScale, 0.0f * texcoordScale}, { 1.0f, 1.0f, 1.0f, 1.0f } },
		{ {-1.0f * boxSize, -1.0f * boxSize,  1.0f * boxSize}, {0.0f, -1.0f,  0.0f}, {0.0f * texcoordScale, 1.0f * texcoordScale}, { 1.0f, 1.0f, 1.0f, 1.0f } },
		{ {-1.0f * boxSize, -1.0f * boxSize,  1.0f * boxSize}, {0.0f, -1.0f,  0.0f}, {0.0f * texcoordScale, 1.0f * texcoordScale}, { 1.0f, 1.0f, 1.0f, 1.0f } },
		{ {1.0f * boxSize, -1.0f * boxSize, -1.0f * boxSize},  {0.0f, -1.0f,  0.0f}, {1.0f * texcoordScale, 0.0f * texcoordScale}, { 1.0f, 1.0f, 1.0f, 1.0f } },
		{ {1.0f * boxSize, -1.0f * boxSize,  1.0f * boxSize},  {0.0f, -1.0f,  0.0f}, {1.0f * texcoordScale, 1.0f * texcoordScale}, { 1.0f, 1.0f, 1.0f, 1.0f } }
	};

	cubeModel.mesh.vertices = cubeVertices;

	vertexCount = static_cast<uint32_t>(cubeVertices.size());

	vertexBufferSize = static_cast<uint32_t>(sizeof(DXVertex) * vertexCount);

	//立方体索引数组
	std::vector<uint32_t> cubeIndices = {
		0, 1, 2,
		3, 4, 5,

		6, 7, 8,
		9, 10, 11,

		12, 13, 14,
		15, 16, 17,

		18, 19, 20,
		21, 22, 23,

		24, 25, 26,
		27, 28, 29,

		30, 31, 32,
		33, 34, 35,
	};

	cubeModel.mesh.indices = cubeIndices;

	indexCount = static_cast<uint32_t>(cubeIndices.size());

	indexBufferSize = static_cast<uint32_t>(sizeof(uint32_t) * indexCount);
}

void D3DApp::loadResources()
{
	auto imageData = loadImage(L"Assets/Textures/Kanna.jpg");
	//auto imageData = loadImage(L"Assets/Textures/Skrik.bmp");

	// 创建纹理的默认堆
	D3D12_HEAP_DESC textureHeapDesc{};
	// 为堆指定纹理图片至少2倍大小的空间，这里没有详细取计算了，只是指定了一个足够大的空间，够放纹理就行
	// 实际应用中也是要综合考虑分配堆的大小，以便可以重用堆
	textureHeapDesc.SizeInBytes = ROUND_UP(2 * imageData.rowPitch * imageData.width, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);;
	// 指定堆的对齐方式，这里使用了默认的64K边界对齐，因为我们暂时不需要MSAA支持
	textureHeapDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
	textureHeapDesc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
	textureHeapDesc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	textureHeapDesc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	// 拒绝渲染目标纹理，拒绝深度模板纹理，实际就只是用来摆放普通纹理
	textureHeapDesc.Flags = D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES | D3D12_HEAP_FLAG_DENY_BUFFERS;

	DXCheck(device->CreateHeap(&textureHeapDesc, IID_PPV_ARGS(&textureHeap)), "ID3D12Device::CreateHealp failed!");

	// 创建2D纹理
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

	// 使用“定位方式”来创建纹理，注意下面这个调用内部实际已经没有存储分配和释放的实际操作了，所以性能很高
	// 同时可以在这个堆上反复调用CreatePlacedResource来创建不同的纹理，当然前提是它们不在倍使用的时候，才考虑重用堆
	DXCheck(device->CreatePlacedResource(textureHeap.Get(),
		0,
		&textureDesc,	// 可以使用CD3DX12_RESOURCE_DESC::Tex2D来简化结构体的初始化
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&texture)), "ID3D12Device::CreatePlacedResource failed!");

	// 获取上传堆资源缓冲的大小，这个尺寸通常大于实际图片的尺寸
	textureUploadBufferSize = GetRequiredIntermediateSize(texture.Get(), 0, 1);

	// 使用“定位方式”创建用于上传纹理数据的缓冲资源

	// 创建用于上传纹理的资源, 注意其类型是Buffer
	// 上传堆对于GPU访问来说性能是很差的，
	// 所以对于几乎不变的数据尤其像纹理都是
	// 通过它来上传至GPU访问更高效的默认堆中
	ComPtr<ID3D12Resource> textureUpload;

	DXCheck(device->CreatePlacedResource(uploadHeap.Get(),
		0,
		&CD3DX12_RESOURCE_DESC::Buffer(textureUploadBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&textureUpload)), "ID3D12Device::CreatePlacedResource failed!");

	// 按照资源缓冲大小来分配实际图片数据存储的内存大小
	void* textureData = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, textureUploadBufferSize);

	if (textureData == nullptr)
	{
		DXThrow("HeapAlloc failed!");
	}

	// 从图片中读取数据
	DXCheck(imageData.data->CopyPixels(
		nullptr,
		imageData.rowPitch,
		//注意这里才是图片数据真实的大小，这个值通常小于缓冲的大小
		static_cast<uint32_t>(imageData.rowPitch * imageData.width),
		reinterpret_cast<uint8_t*>(textureData)), "IWICBitmapSource::CopyPixels failed!");

	//{//下面这段代码来自DX12的示例，直接通过填充缓冲绘制了一个黑白方格的纹理
	// //还原这段代码，然后注释上面的CopyPixels调用可以看到黑白方格纹理的效果
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

	// 获取向上传堆拷贝纹理数据的一些纹理转换尺寸信息
	// 对于复杂的DDS纹理这是非常必要的过程
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

	// 因为上传堆实际就是CPU传递数据到GPU的中介
	// 所以我们可以使用熟悉的Map方法将它先映射到CPU内存地址中
	// 然后我们按行将数据复制到上传堆中
	// 需要注意的是之所以按行拷贝是因为GPU资源的行大小
	// 与实际图片的行大小是有差异的,二者的内存边界对齐要求是不一样的
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

	// 取消映射 对于易变的数据如每帧的变换矩阵等数据，可以撒懒不用Unmap了，
	// 让它常驻内存,以提高整体性能，因为每次Map和Unmap是很耗时的操作
	// 因为现在起码都是64位系统和应用了，地址空间是足够的，被长期占用不会影响什么
	textureUpload->Unmap(0, nullptr);

	// 释放图片数据，做一个干净的程序员
	HeapFree(GetProcessHeap(), 0, textureData);

	// 向命令队列发出从上传堆复制纹理数据到默认堆的命令
	CD3DX12_TEXTURE_COPY_LOCATION dest(texture.Get(), 0);
	CD3DX12_TEXTURE_COPY_LOCATION src(textureUpload.Get(), textureLayout);

	commandList->CopyTextureRegion(&dest, 0, 0, 0, &src, nullptr);

	// 设置一个资源屏障，同步并确认复制操作完成
	// 直接使用结构体然后调用的形式
	D3D12_RESOURCE_BARRIER resourceBarrier{};
	resourceBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	resourceBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	resourceBarrier.Transition.pResource = texture.Get();
	resourceBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	resourceBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	resourceBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

	commandList->ResourceBarrier(1, &resourceBarrier);

	// 或者使用D3DX12库中的工具类调用的等价形式，下面的方式更简洁一些
	// pICommandList->ResourceBarrier(1
	//	 , &CD3DX12_RESOURCE_BARRIER::Transition(pITexcute.Get()
	//	 , D3D12_RESOURCE_STATE_COPY_DEST
	//	 , D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
	// );

	// 最终创建SRV描述符
	D3D12_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc{};
	shaderResourceViewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	shaderResourceViewDesc.Format = textureDesc.Format;
	shaderResourceViewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	shaderResourceViewDesc.Texture2D.MipLevels = 1;

	// 在描述符堆中创建SRV描述符
	device->CreateShaderResourceView(texture.Get(), &shaderResourceViewDesc, shaderResourceViewDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	// 执行命令列表并等待纹理资源上传完成，这一步是必须的
	DXCheck(commandList->Close(), "ID3D12GraphicsCommandList::Close failed!");
	ID3D12CommandList* commandLists[] = { commandList.Get() };
	commandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);

	// Fence初始值要大于0，否则后面的if (fence->GetCompletedValue() < currentFenceValue)永远为false
	fenceValue = 1;

	const uint64_t currentFenceValue = fenceValue;

	DXCheck(commandQueue->Signal(fence.Get(), currentFenceValue), "ID3D12CommandQueue::Signal failed!");
	fenceValue++;

	// 看命令有没有真正执行到围栏标记的这里，没有就利用事件去等待，注意使用的是命令队列对象的指针
	if (fence->GetCompletedValue() < currentFenceValue)
	{
		DXCheck(fence->SetEventOnCompletion(currentFenceValue, fenceEvent), "ID3D12Fence::SetEventOnCompletion failed!");
		WaitForSingleObject(fenceEvent, INFINITE);
	}

	//命令分配器先Reset一下，刚才已经执行过了一个复制纹理的命令
	DXCheck(commandAllocator->Reset(), "ID3D12CommandAllocator::Reset failed!");

	//Reset命令列表，并重新指定命令分配器和PSO对象
	DXCheck(commandList->Reset(commandAllocator.Get(), graphicsPipelineState.Get()), "ID3D12GraphicsCommandList::Reset failed!");

	loadCubeResource();

	bunny.load("Assets/Models/bunny.obj");

	vertexCount = static_cast<uint32_t>(bunny.mesh.vertices.size());
	vertexBufferSize = bunny.mesh.vertexBufferSize;

	indexCount = static_cast<uint32_t>(bunny.mesh.indices.size());
	indexBufferSize = bunny.mesh.indexBufferSize;
}

void D3DApp::createVertexBuffer(const DXModel& model)
{
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
	//DXCheck(device->CreateCommittedResource(
	//	&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
	//	D3D12_HEAP_FLAG_NONE,
	//	&CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
	//	D3D12_RESOURCE_STATE_GENERIC_READ,
	//	nullptr,
	//	IID_PPV_ARGS(&vertexBuffer)),
	//	"ID3D12Device::CreateCommittedResource failed!");

	// 计算Vertex Buffer在上传堆中的偏移值(上传堆有的大小至少是2 * uploadBufferSize，前一个uploadBufferSize的空间在前面用于上传纹理)
	bufferOffset = ROUND_UP(textureUploadBufferSize, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

	// 使用“定位方式”创建顶点缓冲和索引缓冲，使用与上传纹理数据缓冲相同的上传堆，注意第二个参数指出了堆中的偏移位置
	// 按照堆边界对齐要求，我们主动将偏移位置对齐到了64k的边界上
	DXCheck(device->CreatePlacedResource(
		uploadHeap.Get(),
		bufferOffset,
		&CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&vertexBuffer)),
	"Create vertex buffer failed!");

	// 使用 map-memcpy-unmap 大法将数据传至顶点缓冲对象
	// 注意顶点缓冲使用的是和上传纹理数据缓冲相同的一个堆，这很神奇
	UINT8* vertexDataBegin = nullptr;

	CD3DX12_RANGE readRange(0, 0);
	DXCheck(vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&vertexDataBegin)), "ID3D12Resource::Map failed!");

	memcpy_s(vertexDataBegin, vertexBufferSize, model.mesh.vertices.data(), vertexBufferSize);

	vertexBuffer->Unmap(0, nullptr);

	vertexBufferView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
	vertexBufferView.StrideInBytes = sizeof(DXVertex);
	vertexBufferView.SizeInBytes = vertexBufferSize;
}

void D3DApp::createIndexBuffer(const DXModel& model)
{
	// 计算边界对齐的正确的偏移位置
	bufferOffset = ROUND_UP(bufferOffset + vertexBufferSize, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

	DXCheck(device->CreatePlacedResource(
		uploadHeap.Get(),
		bufferOffset,
		&CD3DX12_RESOURCE_DESC::Buffer(indexBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&indexBuffer)), "Create index buffer failed!");

	uint8_t* indexDataBegin = nullptr;

	CD3DX12_RANGE readRange(0, 0);
	DXCheck(indexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&indexDataBegin)), "ID3D12Resource::Map failed!");
	memcpy_s(indexDataBegin, indexBufferSize, model.mesh.indices.data(), indexBufferSize);
	indexBuffer->Unmap(0, nullptr);

	indexBufferView.BufferLocation = indexBuffer->GetGPUVirtualAddress();
	indexBufferView.Format = DXGI_FORMAT_R32_UINT;
	indexBufferView.SizeInBytes = indexBufferSize;
}

void D3DApp::createConstantBufferView()
{
	// 以“定位方式”创建常量缓冲
	bufferOffset = ROUND_UP(bufferOffset + indexBufferSize, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

	constantBufferSize = ROUND_UP(sizeof (ModelViewProjectionBuffer), 256) * 256;

	// 创建常量缓冲区，注意缓冲区尺寸设置为256边界对齐大小
	DXCheck(device->CreatePlacedResource(
		uploadHeap.Get(),
		bufferOffset,
		&CD3DX12_RESOURCE_DESC::Buffer(constantBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&constantBuffer)),
		"Create constant buffer failed!");

	// Map之后就不再Unmap了，直接复制数据进去，这样每帧都不用 map-copy-unmap 浪费时间了
	DXCheck(constantBuffer->Map(0, nullptr, reinterpret_cast<void**>(&modelViewProjectionBuffer)), "ID3D12Resource::Map failed!");

	D3D12_CONSTANT_BUFFER_VIEW_DESC constantBufferViewDesc{};
	constantBufferViewDesc.BufferLocation = constantBuffer->GetGPUVirtualAddress();
	constantBufferViewDesc.SizeInBytes = constantBufferSize;

	CD3DX12_CPU_DESCRIPTOR_HANDLE constantBufferViewHandle(shaderResourceViewDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		1,
		device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));

	device->CreateConstantBufferView(&constantBufferViewDesc, constantBufferViewHandle);
}

void D3DApp::createDepthStencilDescriptorHeap()
{
	D3D12_DESCRIPTOR_HEAP_DESC depthStencilDescriptorHeapDesc{};
	depthStencilDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	depthStencilDescriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	depthStencilDescriptorHeapDesc.NumDescriptors = 1;

	DXCheck(device->CreateDescriptorHeap(&depthStencilDescriptorHeapDesc, IID_PPV_ARGS(&depthStencilViewDescriptorHeap)), "Create depth stencil descriptor heap failed!");
}

void D3DApp::createDepthStencilBuffer()
{
	D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc{};
	depthStencilViewDesc.Format = DXGI_FORMAT_D32_FLOAT;
	depthStencilViewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	depthStencilViewDesc.Flags = D3D12_DSV_FLAG_NONE;

	D3D12_CLEAR_VALUE depthStencilOptimizedClearValue{};
	depthStencilOptimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
	depthStencilOptimizedClearValue.DepthStencil.Depth = 1.0f;
	depthStencilOptimizedClearValue.DepthStencil.Stencil = 0;

	DXCheck(device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, 
										    windowWidth, windowHeight, 
									1, 
									0, 
									1, 
									0, 
									D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL),
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&depthStencilOptimizedClearValue,
		IID_PPV_ARGS(&depthStencilBuffer)),
		"Create depth stencil buffer failed!");

	CD3DX12_CPU_DESCRIPTOR_HANDLE depthStencilBufferViewHandle(depthStencilViewDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
	
	device->CreateDepthStencilView(depthStencilBuffer.Get(), &depthStencilViewDesc, depthStencilBufferViewHandle);
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


void D3DApp::initDirect3D()
{
	createDebugLayer();
	createDXGIFactory();
	createDevice();
	createCommandQueue();
	createSwapChain();
	createDescriptorHeap();
	createRenderTargetView();
	createRootSignature();
	createGraphicsPipelineState();
	createCommandList();
	createFence();

	// 创建1M大小的上传堆，纹理，顶点缓冲和索引缓冲都会用到
	createUploadHeap(ROUND_UP(1024 * 1024 * 10, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT));
	loadResources();
	createVertexBuffer(bunny);
	createIndexBuffer(bunny);
	createConstantBufferView();
	createDepthStencilDescriptorHeap();
	createDepthStencilBuffer();
	createSamplerDescriptorHeap();
	createSamplers();
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
	// Rendering
	ImGui::Render();

	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList.Get());
}

void D3DApp::shutdownImGui()
{
	// Cleanup
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

void D3DApp::updateConstantBuffer()
{
	// 准备一个简单的旋转MVP矩阵 让方块转起来
	auto currentTimeDuration = std::chrono::high_resolution_clock::now().time_since_epoch();

	auto currentTime = std::chrono::duration<float, std::chrono::seconds::period>(currentTimeDuration).count();

	//计算旋转的角度：旋转角度(弧度) = 时间(秒) * 角速度(弧度/秒)
	//下面这句代码相当于经典游戏消息循环中的OnUpdate函数中需要做的事情
	modelRotationYAngle += (currentTime - startTime) * angularVelocity;

	startTime = currentTime;

	// 旋转角度是2PI周期的倍数，去掉周期数，只留下相对0弧度开始的小于2PI的弧度即可
	if (modelRotationYAngle > XM_2PI)
	{
		modelRotationYAngle = std::fmodf(modelRotationYAngle, XM_2PI);
	}

	// 模型矩阵model
	XMMATRIX model = XMMatrixRotationY(modelRotationYAngle);

	XMMATRIX view = XMMatrixLookAtLH(eye, center, up);

	XMMATRIX projection = XMMatrixPerspectiveFovLH(XM_PIDIV4, static_cast<float>(windowWidth) / static_cast<float>(windowHeight), 0.1f, 1000.0f);
	
	// 计算model * view * projection矩阵
	XMMATRIX modelViewProjection = model * view * projection;

	model = XMMatrixTranspose(model);

	modelViewProjection = XMMatrixTranspose(modelViewProjection);

	XMStoreFloat4x4(&modelViewProjectionBuffer->model, model);

	XMStoreFloat4x4(&modelViewProjectionBuffer->modelViewProjection, modelViewProjection);
}

void D3DApp::updateFPSCounter()
{
	static auto previousSecondsDuration = std::chrono::high_resolution_clock::now().time_since_epoch();
	static auto previousSeconds = std::chrono::duration<float, std::chrono::seconds::period>(previousSecondsDuration).count();

	auto currentSecondsDuration = std::chrono::high_resolution_clock::now().time_since_epoch();
	auto currentSeconds = std::chrono::duration<float, std::chrono::seconds::period>(currentSecondsDuration).count();

	float elapsedSeconds = currentSeconds - previousSeconds;

	if (elapsedSeconds >= 0.25f)
	{
		previousSeconds = currentSeconds;
		float fps = frameCount / elapsedSeconds;
		auto temp = fmt::format("DirectX 12 [FPS: {0}]", fps);
		SetWindowTextA(mainWindow, temp.c_str());
		frameCount = 0;
	}

	frameCount++;
}

void D3DApp::update()
{
	updateConstantBuffer();
}

void D3DApp::processInput(float deltaTime)
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

	CD3DX12_CPU_DESCRIPTOR_HANDLE depthStencilBufferViewHandle(depthStencilViewDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	commandList->OMSetRenderTargets(1, &renderTargetViewHandle, FALSE, &depthStencilBufferViewHandle);

	commandList->SetGraphicsRootSignature(rootSignature.Get());

	ID3D12DescriptorHeap* descriptorHeaps[] = { shaderResourceViewDescriptorHeap.Get(), samplerDescriptorHeap.Get() };
	commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	commandList->SetGraphicsRootDescriptorTable(0, shaderResourceViewDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

	CD3DX12_GPU_DESCRIPTOR_HANDLE constantBufferViewHandle(shaderResourceViewDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), 1, device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));

	commandList->SetGraphicsRootDescriptorTable(1, constantBufferViewHandle);

	CD3DX12_GPU_DESCRIPTOR_HANDLE samplerDescriptorHandle(samplerDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), currentSamplerNo, samplerDescriptorSize);

	commandList->SetGraphicsRootDescriptorTable(2, samplerDescriptorHandle);

	const float clearColor[]{ 0.4f, 0.6f, 0.9f, 1.0f };
	commandList->ClearRenderTargetView(renderTargetViewHandle, clearColor, 0, nullptr);
	commandList->ClearDepthStencilView(depthStencilBufferViewHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	// 注意我们使用的渲染手法是三角形列表，也就是通常的Mesh网格
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
	commandList->IASetIndexBuffer(&indexBufferView);

	commandList->DrawIndexedInstanced(indexCount, 1, 0, 0, 0);

	ID3D12DescriptorHeap* imGuiDescriptorHeaps[] = { imGuiShaderResourceViewDescriptorHeap.Get() };
	commandList->SetDescriptorHeaps(1, imGuiDescriptorHeaps);

	renderImGui();

	commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(renderTargets[frameIndex].Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	DXCheck(commandList->Close(), "ID3D12GraphicsCommandList::Close failed!");

	ID3D12CommandList* commandLists[]{ commandList.Get() };

	commandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);

	DXCheck(swapChain->Present(1, 0), "IDXGISwapChain::Present failed!");

	//开始同步GPU与CPU的执行，先记录围栏标记值
	const UINT64 targetFenceValue = fenceValue;

	DXCheck(commandQueue->Signal(fence.Get(), targetFenceValue), "ID3D12CommandQueue::Signal failed!");
	fenceValue++;

	// 看命令有没有真正执行到围栏标记的这里，没有就利用事件去等待，注意使用的是命令队列对象的指针
	if (fence->GetCompletedValue() < targetFenceValue)
	{
		DXCheck(fence->SetEventOnCompletion(targetFenceValue, fenceEvent), "ID3D12Fence::SetEventOnCompletion failed!");
		WaitForSingleObject(fenceEvent, INFINITE);
	}

	// 到这里说明一个命令队列完整的执行完了，在这里就代表我们的一帧已经渲染完了，接着准备执行下一帧渲染
	// 获取新的后缓冲序号，因为Present真正完成时后缓冲的序号就更新了
	frameIndex = swapChain->GetCurrentBackBufferIndex();

	// 命令分配器先Reset一下
	DXCheck(commandAllocator->Reset(), "ID3D12CommandAllocator::Reset failed!");

	// Reset命令列表，并重新指定命令分配器和PSO对象
	DXCheck(commandList->Reset(commandAllocator.Get(), graphicsPipelineState.Get()), "ID3D12GraphicsCommandList::Reset failed!");
}

