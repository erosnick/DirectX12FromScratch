#include "pch.h"
#include "7.D3DAppGLFW.h"

#include "framework.h"
#include "pix3.h"
#include "Utils.h"
#include "WICImage.h"
#include <iostream>
#include <fmt/format.h>
#include <windowsx.h>
#include <process.h>
#include <d3dcompiler.h>

#include "DDSTextureLoader12.h"

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "D3D12Slim.h"
#include "UploadBuffer.h"

const uint32_t NumFrameResources = 3;

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

D3DApp::~D3DApp()
{
	flushCommandQueue();
}

bool D3DApp::initialize(HINSTANCE hInstance, int32_t cmdShow)
{
	if (!initWindow(hInstance, cmdShow))
	{
		return false;
	}

	try
	{
		initializeDirect3D();
	}
	catch (const DXException& exception)
	{
		fmt::print("{}\n", exception.ToString());
	}

	renderReady = true;

	return true;
}

int32_t D3DApp::run()
{
	MSG msg{};

	// 记录帧开始时间，和当前时间，以循环结束为界
	static auto startTimeDuration = std::chrono::high_resolution_clock::now().time_since_epoch();
	static auto startTime = std::chrono::duration<double, std::chrono::seconds::period>(startTimeDuration).count();
	currentTime = startTime;
	simulationTime = startTime;

	gameTimer.Reset();

	// Main message loop:
	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();

		if (!appPaused)
		{

			auto realTimeDuration = std::chrono::high_resolution_clock::now().time_since_epoch();
			auto realTime = std::chrono::duration<double, std::chrono::seconds::period>(realTimeDuration).count();

			//while (simulationTime < realTime)
			{
				simulationTime += FrameTime;
				processInput(FrameTime);
				gameTimer.Tick();
				calculateFrameStats();
				update();
			}

			updateImGui();
			onGUIRender();
			render();
		}
		else
		{
			Sleep(100);
		}
	}

	return static_cast<int32_t>(msg.wParam);
}

bool D3DApp::initGLFW()
{
	glfwInit();

	window = glfwCreateWindow(windowWidth, windowHeight, "Land and Ocean", nullptr, nullptr);
	mainWindow = glfwGetWin32Window(window);

	if (window == nullptr)
	{
		return false;
	}

	registerGLFWEventCallbacks();

	return true;
}

bool D3DApp::initWindow(HINSTANCE hInstance, int32_t cmdShow)
{
	return initGLFW();
}

void D3DApp::createDebugLayer()
{
#if defined(_DEBUG)
	ComPtr<ID3D12Debug> debugController;
	DXCheck(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)), L"D3D12GetDebugInterface failed!");
	debugController->EnableDebugLayer();
#endif
}

void D3DApp::createDXGIFactory()
{
	CreateDXGIFactory2(DXGIFactoryFlags, IID_PPV_ARGS(&DXGIFactory));

	DXCheck(DXGIFactory->MakeWindowAssociation(mainWindow, DXGI_MWA_NO_ALT_ENTER), L"IDXGIFactory::MakeWindowAssociation failed!");
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
	DXCheck(D3D12CreateDevice(DXGIAdapter.Get(), D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&device)), L"D3D12CreateDevice failed!");

	// 缓存各种描述符的尺寸
	renderTargetViewDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	SRVCBVUAVDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	depthStencilViewDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	samplerDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
}

void D3DApp::createCommandQueue()
{
	D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
	commandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	DXCheck(device->CreateCommandQueue(&commandQueueDesc,
		IID_PPV_ARGS(&commandQueue)),
		L"ID3D12Device::CreateCommandQueue failed!");
}

void D3DApp::createSwapChain()
{
	//DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
	//swapChainDesc.BufferCount = FrameBackbufferCount;
	//swapChainDesc.Width = windowWidth;
	//swapChainDesc.Height = windowHeight;
	//swapChainDesc.Format = swapChainFormat;
	//swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	//swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	//swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
	//swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
	//swapChainDesc.SampleDesc.Count = 1;
	////swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

	////DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullScreenSwapChainDesc = {};
	////fullScreenSwapChainDesc.Windowed = TRUE;

	//DXCheck(DXGIFactory->CreateSwapChainForHwnd(commandQueue.Get(),
	//mainWindow,
	//&swapChainDesc,
	////&fullScreenSwapChainDesc,
	//nullptr,
	//nullptr, &swapChain1),
	//L"DXGIFactory::CreateSwapChainForHwnd failed!");

	//DXCheck(swapChain1.As(&swapChain), L"IDXGISwapChain::As(&swapChain) failed");

	DXGI_SWAP_CHAIN_DESC swapChainDesc;
	swapChainDesc.BufferDesc.Width = windowWidth;
	swapChainDesc.BufferDesc.Height = windowHeight;
	swapChainDesc.BufferDesc.RefreshRate.Numerator = 60;
	swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
	swapChainDesc.BufferDesc.Format = swapChainFormat;
	swapChainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	swapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.SampleDesc.Quality = 0;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = FrameBackbufferCount;
	swapChainDesc.OutputWindow = mainWindow;
	swapChainDesc.Windowed = true;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	DXCheck(DXGIFactory->CreateSwapChain(
		commandQueue.Get(),
		&swapChainDesc,
		swapChain.GetAddressOf()),
		L"DXGIFactory::CreateSwapChainForHwnd failed!");

	// 得到当前后缓冲区的序号，也就是下一个将要呈送显示的缓冲区的序号
	// 注意此处使用了高版本的SwapChain接口的函数
	//currentFrameIndex = swapChain->GetCurrentBackBufferIndex();
	currentFrameIndex = (currentFrameIndex + 1) % FrameBackbufferCount;
}

void D3DApp::createShaderResourceViewDescriptorHeap()
{
	createDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, FrameBackbufferCount, false, renderTargetViewDescriptorHeap, L"renderTargetViewDescriptorHeap");

	// 创建用于纹理采样的描述符堆(这里单独为ImGUI创建了一个，避免冲突)
	D3D12_DESCRIPTOR_HEAP_DESC shaderResourceViewDescriptorHeapDesc{};
	shaderResourceViewDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	shaderResourceViewDescriptorHeapDesc.NumDescriptors = 10;
	shaderResourceViewDescriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	DXCheck(device->CreateDescriptorHeap(&shaderResourceViewDescriptorHeapDesc, IID_PPV_ARGS(&imGuiShaderResourceViewDescriptorHeap)), L"ID3D12Device::CreateDescriptorHeap failed!");

	// 因为引入了FrameResource，因此这里的NumDescriptors = n个SRV + (RenderItem数量 + 1) * NumFrameResources个CBV
	// 上面的+1表示PassConstants
	objectCount = static_cast<uint32_t>(allRenderItems.size());
	materialCount = static_cast<uint32_t>(materials.size());

	// Save an offset to the start of the pass CBVs.  These are the last 3 descriptors.
	passConstantBufferViewOffset = (objectCount + materialCount) * NumFrameResources + textureCount;

	auto numDescriptors = textureCount + (objectCount + materialCount + 2) * NumFrameResources;

	shaderResourceViewDescriptorHeapDesc.NumDescriptors = numDescriptors;

	DXCheck(device->CreateDescriptorHeap(&shaderResourceViewDescriptorHeapDesc, IID_PPV_ARGS(&shaderResourceViewDescriptorHeap)), L"ID3D12Device::CreateDescriptorHeap failed!");
}

void D3DApp::createDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors, bool shaderVisible, ComPtr<ID3D12DescriptorHeap>& descriptorHeap, const std::wstring& name)
{
	D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};
	descriptorHeapDesc.Type = type;
	descriptorHeapDesc.NumDescriptors = numDescriptors;
	descriptorHeapDesc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	std::wstring errorMesssage = L"";

	if (name.empty())
	{
		errorMesssage = std::wstring(L"Create ") + std::wstring(ObjectName(descriptorHeap)) + std::wstring(L"failed!");
	}
	else
	{
		errorMesssage = std::wstring(L"Create ") + name + std::wstring(L"failed!");
	}

	DXCheck(device->CreateDescriptorHeap(&descriptorHeapDesc,
		IID_PPV_ARGS(descriptorHeap.GetAddressOf())), errorMesssage.c_str());
}

Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> D3DApp::createDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors, bool shaderVisible, const std::wstring& name /*= L""*/)
{
	D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};
	descriptorHeapDesc.Type = type;
	descriptorHeapDesc.NumDescriptors = numDescriptors;
	descriptorHeapDesc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	std::wstring errorMesssage = L"";

	if (name.empty())
	{
		errorMesssage = std::wstring(L"Create ") + std::wstring(ObjectName(descriptorHeap)) + std::wstring(L"failed!");
	}
	else
	{
		errorMesssage = std::wstring(L"Create ") + name + std::wstring(L"failed!");
	}

	ID3D12DescriptorHeap* descriptorHeap;

	DXCheck(device->CreateDescriptorHeap(&descriptorHeapDesc,
		IID_PPV_ARGS(&descriptorHeap)), errorMesssage.c_str());

	return descriptorHeap;
}

void D3DApp::createSampler(D3D12_FILTER filter, D3D12_TEXTURE_ADDRESS_MODE addressMode, const ComPtr<ID3D12DescriptorHeap>& descriptorHeap, uint32_t index)
{
	D3D12_SAMPLER_DESC samplerDesc{};
	samplerDesc.Filter = filter;
	samplerDesc.MinLOD = 0;
	samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
	samplerDesc.MaxAnisotropy = 1;
	samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	samplerDesc.AddressU = addressMode;
	samplerDesc.AddressV = addressMode;
	samplerDesc.AddressW = addressMode;

	CD3DX12_CPU_DESCRIPTOR_HANDLE samplerDescriptorHeapHandle(
		CPU_DESCRIPTOR_HEAP_START(samplerDescriptorHeap),
		index,
		samplerDescriptorSize);

	device->CreateSampler(&samplerDesc, samplerDescriptorHeapHandle);
}

void D3DApp::createShaderResourceView(TextureDimension dimension, const ComPtr<ID3D12Resource>& texture, const ComPtr<ID3D12DescriptorHeap>& descriptorHeap, uint32_t index)
{
	D3D12_RESOURCE_DESC textureDesc = texture->GetDesc();

	D3D12_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc{};
	shaderResourceViewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	shaderResourceViewDesc.Format = textureDesc.Format;
	if (dimension == TextureDimension::TextureCube)
	{
		shaderResourceViewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
		shaderResourceViewDesc.TextureCube.MipLevels = textureDesc.MipLevels;
	}
	else if (dimension == TextureDimension::Texture2D)
	{
		shaderResourceViewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		shaderResourceViewDesc.Texture2D.MipLevels = textureDesc.MipLevels;
	}
	else if (dimension == TextureDimension::TextureArray)
	{
		shaderResourceViewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
		shaderResourceViewDesc.Texture2DArray.MostDetailedMip = 0;
		shaderResourceViewDesc.Texture2DArray.MipLevels = -1;
		shaderResourceViewDesc.Texture2DArray.FirstArraySlice = 0;
		shaderResourceViewDesc.Texture2DArray.ArraySize = texture->GetDesc().DepthOrArraySize;
	}

	CD3DX12_CPU_DESCRIPTOR_HANDLE descriptorHandle(
		CPU_DESCRIPTOR_HEAP_START(descriptorHeap),
		index,
		SRVCBVUAVDescriptorSize);

	device->CreateShaderResourceView(texture.Get(), &shaderResourceViewDesc, descriptorHandle);
}

std::unique_ptr<MeshGeometry> D3DApp::createMeshGeometry(const DXModel& model)
{
	auto localMeshGeometry = std::make_unique<MeshGeometry>();
	localMeshGeometry->name = model.name;

	auto vertexBufferSize = model.mesh.vertexBufferSize;
	DXCheck(D3DCreateBlob(vertexBufferSize, &localMeshGeometry->vertexBufferCPU), L"D3DCreateBlob failed!");
	CopyMemory(localMeshGeometry->vertexBufferCPU->GetBufferPointer(), model.mesh.vertices.data(), vertexBufferSize);

	auto indexBufferSize = model.mesh.indexBufferSize;
	DXCheck(D3DCreateBlob(indexBufferSize, &localMeshGeometry->indexBufferCPU), L"D3DCreateBlob failed!");
	CopyMemory(localMeshGeometry->indexBufferCPU->GetBufferPointer(), model.mesh.vertices.data(), indexBufferSize);

	localMeshGeometry->vertexBufferGPU = Utils::createDefaultBuffer(device.Get(), 
																	commandListDirectPre.Get(),
															model.mesh.vertices.data(), 
															vertexBufferSize, 
														localMeshGeometry->vertexBufferUploader);

	localMeshGeometry->indexBufferGPU = Utils::createDefaultBuffer(device.Get(),
																   commandListDirectPre.Get(),
														   model.mesh.indices.data(),
														   indexBufferSize,
													   localMeshGeometry->indexBufferUploader);
	
	localMeshGeometry->vertexByteStride = sizeof(DXVertex);
	localMeshGeometry->vertexBufferByteSize = model.mesh.vertexBufferSize;
	localMeshGeometry->indexFormat = DXGI_FORMAT_R32_UINT;
	localMeshGeometry->indexBufferByteSize = model.mesh.indexBufferSize;

	SubmeshGeometry submesh;
	submesh.indexCount = static_cast<uint32_t>(model.mesh.indices.size());
	submesh.startIndexLocation = 0;
	submesh.baseVertexLocation = 0;

	localMeshGeometry->drawArgs[model.name] = submesh;

	return localMeshGeometry;
}

void D3DApp::createMeshDataGeometry(const GeometryGenerator::MeshData& meshData, const std::string& name)
{
	auto model = GeometryGenerator::toDXModel(meshData, name);

	auto meshGeometry = createMeshGeometry(model);

	meshGeometries[meshGeometry->name] = std::move(meshGeometry);
}

void D3DApp::createOceanMeshGeometry()
{
	std::vector<std::uint16_t> indices(3 * waves->TriangleCount()); // 3 indices per face
	assert(waves->VertexCount() < 0x0000ffff);

	// Iterate over each quad.
	int m = waves->RowCount();
	int n = waves->ColumnCount();
	int k = 0;
	for (int i = 0; i < m - 1; ++i)
	{
		for (int j = 0; j < n - 1; ++j)
		{
			indices[k] = (i + 1)* n + j;
			indices[k + 1] = i * n + j + 1;
			indices[k + 2] = i * n + j;

			indices[k + 3] = (i + 1) * n + j + 1;
			indices[k + 4] = i * n + j + 1;
			indices[k + 5] = (i + 1)* n + j;

			k += 6; // next quad
		}
	}

	UINT vertexBufferByteSize = waves->VertexCount() * sizeof(DXVertex);
	UINT indexBufferByteSize = static_cast<uint32_t>(indices.size()) * sizeof(std::uint16_t);

	auto meshGeometry = std::make_unique<MeshGeometry>();
	meshGeometry->name = "Ocean";

	// Set dynamically.
	meshGeometry->vertexBufferCPU = nullptr;
	meshGeometry->vertexBufferGPU = nullptr;

	DXCheck(D3DCreateBlob(indexBufferByteSize, &meshGeometry->indexBufferCPU), L"D3DCreateBlob failed!");
	CopyMemory(meshGeometry->indexBufferCPU->GetBufferPointer(), indices.data(), indexBufferByteSize);

	meshGeometry->indexBufferGPU = Utils::createDefaultBuffer(device.Get(),
		commandListDirectPre.Get(), indices.data(), indexBufferByteSize, meshGeometry->indexBufferUploader);

	meshGeometry->vertexByteStride = sizeof(DXVertex);
	meshGeometry->vertexBufferByteSize = vertexBufferByteSize;
	meshGeometry->indexFormat = DXGI_FORMAT_R16_UINT;
	meshGeometry->indexBufferByteSize = indexBufferByteSize;

	SubmeshGeometry submesh;
	submesh.indexCount = (UINT)indices.size();
	submesh.startIndexLocation = 0;
	submesh.baseVertexLocation = 0;

	meshGeometry->drawArgs["Ocean"] = submesh;

	meshGeometries["Ocean"] = std::move(meshGeometry);

	vertexCount += waves->VertexCount();
	indexCount += static_cast<uint32_t>(indices.size());
}

void D3DApp::createTreeSpritesGeometry(uint32_t treeCount)
{
	std::vector<TreeSpriteVertex> vertices;
	vertices.resize(treeCount);

	for (UINT i = 0; i < treeCount; ++i)
	{
		float x = MathHelper::RandomF(-45.0f, 45.0f);
		float z = MathHelper::RandomF(-45.0f, 45.0f);
		float y = getHillsHeight(x, z);

		// Move tree slightly above land height.
		y += 4.0f;

		vertices[i].position = { x, y, z };
		vertices[i].size = { 10.0f, 10.0f };
	}

	std::vector<std::uint16_t> indices;

	for (uint16_t index = 0; index < treeCount; index++)
	{
		indices.push_back(index);
	}

	const UINT vertexBufferByteSize = (UINT)vertices.size() * sizeof(TreeSpriteVertex);
	const UINT indexBufferByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto meshGeometry = std::make_unique<MeshGeometry>();
	meshGeometry->name = "TreeSpritesGeometry";

	DXCheck(D3DCreateBlob(vertexBufferByteSize, &meshGeometry->vertexBufferCPU), L"D3DCreateBlob failed!");
	CopyMemory(meshGeometry->vertexBufferCPU->GetBufferPointer(), vertices.data(), vertexBufferByteSize);

	DXCheck(D3DCreateBlob(indexBufferByteSize, &meshGeometry->indexBufferCPU), L"D3DCreateBlob failed!");
	CopyMemory(meshGeometry->indexBufferCPU->GetBufferPointer(), indices.data(), indexBufferByteSize);

	meshGeometry->vertexBufferGPU = Utils::createDefaultBuffer(device.Get(),
		commandListDirectPre.Get(), vertices.data(), vertexBufferByteSize, meshGeometry->vertexBufferUploader);

	meshGeometry->indexBufferGPU = Utils::createDefaultBuffer(device.Get(),
		commandListDirectPre.Get(), indices.data(), indexBufferByteSize, meshGeometry->indexBufferUploader);

	meshGeometry->vertexByteStride = sizeof(TreeSpriteVertex);
	meshGeometry->vertexBufferByteSize = vertexBufferByteSize;
	meshGeometry->indexFormat = DXGI_FORMAT_R16_UINT;
	meshGeometry->indexBufferByteSize = indexBufferByteSize;

	SubmeshGeometry submesh;
	submesh.indexCount = (UINT)indices.size();
	submesh.startIndexLocation = 0;
	submesh.baseVertexLocation = 0;

	meshGeometry->drawArgs["Points"] = submesh;

	meshGeometries["TreeSpritesGeometry"] = std::move(meshGeometry);
}

void D3DApp::createSkyboxDescriptorHeap()
{
	createDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
						3,	// 1 SRV + 2 CBV
						true,
						skyboxDescriptorHeap, ObjectName(skyboxDescriptorHeap));
}

void D3DApp::createSkyboxDescriptors()
{
	createShaderResourceView(TextureDimension::TextureCube, textures["Skybox"]->resource, skyboxDescriptorHeap);
}

void D3DApp::createRenderTargetView()
{
	CD3DX12_CPU_DESCRIPTOR_HANDLE renderTargetViewDescriptorHandle(CPU_DESCRIPTOR_HEAP_START(renderTargetViewDescriptorHeap));

	for (auto i = 0; i < FrameBackbufferCount; i++)
	{
		DXCheck(swapChain->GetBuffer(i, IID_PPV_ARGS(&renderTargets[i])), L"IDXGISwapChain::GetBuffer failed!");
		renderTargets[i]->SetName(L"renderTarget");
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
	CD3DX12_DESCRIPTOR_RANGE1 descriptorRanges[5]{};

	descriptorRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE);
	descriptorRanges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
	descriptorRanges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1);
	descriptorRanges[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 2);
	descriptorRanges[4].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0);

	CD3DX12_ROOT_PARAMETER1 rootParameters[5]{};
	rootParameters[0].InitAsDescriptorTable(1, &descriptorRanges[0], D3D12_SHADER_VISIBILITY_PIXEL);		// SRV仅 Pixel Shader 可见
	rootParameters[1].InitAsDescriptorTable(1, &descriptorRanges[1], D3D12_SHADER_VISIBILITY_ALL);		// CBV是所有 Shader 可见
	rootParameters[2].InitAsDescriptorTable(1, &descriptorRanges[2], D3D12_SHADER_VISIBILITY_ALL);		// CBV是所有 Shader 可见
	rootParameters[3].InitAsDescriptorTable(1, &descriptorRanges[3], D3D12_SHADER_VISIBILITY_ALL);		// CBV是所有 Shader 可见
	rootParameters[4].InitAsDescriptorTable(1, &descriptorRanges[4], D3D12_SHADER_VISIBILITY_PIXEL);		// Sampler 仅 Pixel Shader 可见

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
	rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPtr<ID3DBlob> rootSignatureData;
	ComPtr<ID3DBlob> errorData;

	DXCheck(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &rootSignatureData, &errorData), L"D3DX12SerializeVersionedRootSignature failed!");

	DXCheck(device->CreateRootSignature(0,
		rootSignatureData->GetBufferPointer(),
		rootSignatureData->GetBufferSize(),
		IID_PPV_ARGS(&rootSignature)),
		L"ID3D12Device::CreateRootSignature failed!");

	DXCheck(device->CreateRootSignature(0,
		rootSignatureData->GetBufferPointer(),
		rootSignatureData->GetBufferSize(),
		IID_PPV_ARGS(&renderTextureRootSignature)),
		L"ID3D12Device::CreateRootSignature failed!");
}

void D3DApp::createShadersAndInputlayouts()
{
	if (compileOnTheFly)
	{
		try
		{
			ShaderCompileParameters vertexShaderParameters;
			vertexShaderParameters.path = L"Assets/Shaders/LandAndOcean.hlsl";
			vertexShaderParameters.entryPoint = L"VSMain";
			vertexShaderParameters.targetProfile = L"vs_6_0";
			vertexShaderParameters.defines = {};
			Utils::compileShader(vertexShaderParameters, shaders["LandAndOceanVS"]);

			ShaderCompileParameters pixelShaderParameters;
			pixelShaderParameters.path = L"Assets/Shaders/LandAndOcean.hlsl";
			pixelShaderParameters.entryPoint = L"PSMain";
			pixelShaderParameters.targetProfile = L"ps_6_0";
			pixelShaderParameters.defines = { L"FOG" };
			Utils::compileShader(pixelShaderParameters, shaders["LandAndOceanPS"]);
		}
		catch (DXException& exception)
		{
			fmt::print("{}\n", exception.ToString());
		}
	}
	else
	{
		shaders["LandAndOceanVS"] = reinterpret_cast<IDxcBlob*>(Utils::loadShaderBinary("Assets/Shaders/LandAndOceanVS.dxil"));
		shaders["LandAndOceanPS"] = reinterpret_cast<IDxcBlob*>(Utils::loadShaderBinary("Assets/Shaders/LandAndOceanPS.dxil"));
	}

	if (compileOnTheFly)
	{
		try
		{
			ShaderCompileParameters alphaTestedVertexShaderParameters;
			alphaTestedVertexShaderParameters.path = L"Assets/Shaders/LandAndOcean.hlsl";
			alphaTestedVertexShaderParameters.entryPoint = L"VSMain";
			alphaTestedVertexShaderParameters.targetProfile = L"vs_6_0";
			alphaTestedVertexShaderParameters.defines = {};
			Utils::compileShader(alphaTestedVertexShaderParameters, shaders["AlphaTestedVS"]);

			ShaderCompileParameters alphaTestedPixelShaderParameters;
			alphaTestedPixelShaderParameters.path = L"Assets/Shaders/LandAndOcean.hlsl";
			alphaTestedPixelShaderParameters.entryPoint = L"PSMain";
			alphaTestedPixelShaderParameters.targetProfile = L"ps_6_0";
			alphaTestedPixelShaderParameters.defines = { L"ALPHA_TEST", L"FOG" };
			Utils::compileShader(alphaTestedPixelShaderParameters, shaders["AlphaTestedPS"]);
		}
		catch (DXException& exception)
		{
			fmt::print("{}\n", exception.ToString());
		}
	}
	else
	{
		shaders["AlphaTestedVS"] = reinterpret_cast<IDxcBlob*>(Utils::loadShaderBinary("Assets/Shaders/LandAndOceanVS.dxil"));
		shaders["AlphaTestedPS"] = reinterpret_cast<IDxcBlob*>(Utils::loadShaderBinary("Assets/Shaders/LandAndOceanPS.dxil"));
	}

	if (compileOnTheFly)
	{
		try
		{
			ShaderCompileParameters noAlphaVertexShaderParameters;
			noAlphaVertexShaderParameters.path = L"Assets/Shaders/LandAndOcean.hlsl";
			noAlphaVertexShaderParameters.entryPoint = L"VSMain";
			noAlphaVertexShaderParameters.targetProfile = L"vs_6_0";
			noAlphaVertexShaderParameters.defines = {};
			Utils::compileShader(noAlphaVertexShaderParameters, shaders["NoAlphaVS"]);

			ShaderCompileParameters noAlphaPixelShaderParameters;
			noAlphaPixelShaderParameters.path = L"Assets/Shaders/LandAndOcean.hlsl";
			noAlphaPixelShaderParameters.entryPoint = L"PSMain";
			noAlphaPixelShaderParameters.targetProfile = L"ps_6_0";
			noAlphaPixelShaderParameters.defines = { L"ALPHA_TEST", L"FOG", L"NO_ALPHA" };
			Utils::compileShader(noAlphaPixelShaderParameters, shaders["NoAlphaPS"]);
		}
		catch (DXException& exception)
		{
			fmt::print("{}\n", exception.ToString());
		}
	}
	else
	{
		shaders["NoAlphaVS"] = reinterpret_cast<IDxcBlob*>(Utils::loadShaderBinary("Assets/Shaders/LandAndOceanVS.dxil"));
		shaders["NoAlphaPS"] = reinterpret_cast<IDxcBlob*>(Utils::loadShaderBinary("Assets/Shaders/LandAndOceanPS.dxil"));
	}

	inputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	TCHAR* shaderFileName = L"Assets/Shaders/7.Skybox.hlsl";

	if (compileOnTheFly)
	{
		try
		{
			ShaderCompileParameters vertexShaderParameters;
			vertexShaderParameters.path = shaderFileName;
			vertexShaderParameters.entryPoint = L"VSMain";
			vertexShaderParameters.targetProfile = L"vs_6_0";
			vertexShaderParameters.defines = {};
			Utils::compileShader(vertexShaderParameters, shaders["SkyboxVS"]);

			ShaderCompileParameters pixelShaderParameters;
			pixelShaderParameters.path = shaderFileName;
			pixelShaderParameters.entryPoint = L"PSMain";
			pixelShaderParameters.targetProfile = L"ps_6_0";
			pixelShaderParameters.defines = { L"FOG"};
			Utils::compileShader(pixelShaderParameters, shaders["SkyboxPS"]);
		}
		catch (DXException& exception)
		{
			fmt::print("{}\n", exception.ToString());
		}
	}
	else
	{
		shaders["SkyboxVS"] = reinterpret_cast<IDxcBlob*>(Utils::loadShaderBinary("Assets/Shaders/7.SkyboxVS.dxil"));
		shaders["SkyboxPS"] = reinterpret_cast<IDxcBlob*>(Utils::loadShaderBinary("Assets/Shaders/7.SkyboxPS.dxil"));
	}

	// 天空盒子只有顶点，只有位置参数
	skyboxInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	if (compileOnTheFly)
	{
		try
		{
			ShaderCompileParameters vertexShaderParameters;
			vertexShaderParameters.path = L"Assets/Shaders/TreeSprite.hlsl";
			vertexShaderParameters.entryPoint = L"VSMain";
			vertexShaderParameters.targetProfile = L"vs_6_0";
			vertexShaderParameters.defines = {};
			Utils::compileShader(vertexShaderParameters, shaders["TreeSpriteVS"]);

			ShaderCompileParameters pixelShaderParameters;
			pixelShaderParameters.path = L"Assets/Shaders/TreeSprite.hlsl";
			pixelShaderParameters.entryPoint = L"PSMain";
			pixelShaderParameters.targetProfile = L"ps_6_0";
			pixelShaderParameters.defines = { L"FOG", L"ALPHA_TEST" };
			Utils::compileShader(pixelShaderParameters, shaders["TreeSpritePS"]);

			ShaderCompileParameters geometryShaderParameters;
			geometryShaderParameters.path = L"Assets/Shaders/TreeSprite.hlsl";
			geometryShaderParameters.entryPoint = L"GSMain";
			geometryShaderParameters.targetProfile = L"gs_6_0";
			geometryShaderParameters.defines = { L"FOG" };
			Utils::compileShader(geometryShaderParameters, shaders["TreeSpriteGS"]);
		}
		catch (DXException& exception)
		{
			fmt::print("{}\n", exception.ToString());
		}
	}
	else
	{
		shaders["TreeSpriteVS"] = reinterpret_cast<IDxcBlob*>(Utils::loadShaderBinary("Assets/Shaders/TreeSpriteVS.dxil"));
		shaders["TreeSpritePS"] = reinterpret_cast<IDxcBlob*>(Utils::loadShaderBinary("Assets/Shaders/TreeSpritePS.dxil"));
		shaders["TreeSpriteGS"] = reinterpret_cast<IDxcBlob*>(Utils::loadShaderBinary("Assets/Shaders/TreeSpriteGS.dxil"));
	}

	treeSpriteInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "SIZE", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};
}

void D3DApp::createGraphicsPipelineState()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePipelineStateDesc{};
	opaquePipelineStateDesc.InputLayout = { inputLayout.data(), static_cast<uint32_t>(inputLayout.size()) };
	opaquePipelineStateDesc.pRootSignature = rootSignature.Get();
	opaquePipelineStateDesc.VS = { shaders["LandAndOceanVS"]->GetBufferPointer(), shaders["LandAndOceanVS"]->GetBufferSize() };
	opaquePipelineStateDesc.PS = { shaders["LandAndOceanPS"]->GetBufferPointer(), shaders["LandAndOceanPS"]->GetBufferSize() };

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
	opaquePipelineStateDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	opaquePipelineStateDesc.RasterizerState.FrontCounterClockwise = TRUE;

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
	opaquePipelineStateDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	opaquePipelineStateDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	opaquePipelineStateDesc.DSVFormat = depthStencilFormat;
	opaquePipelineStateDesc.SampleMask = UINT_MAX;
	opaquePipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	opaquePipelineStateDesc.NumRenderTargets = 1;
	opaquePipelineStateDesc.RTVFormats[0] = swapChainFormat;
	opaquePipelineStateDesc.SampleDesc.Count = 1;

	DXCheck(device->CreateGraphicsPipelineState(&opaquePipelineStateDesc,
		IID_PPV_ARGS(&pipelineStates["Opaque"])),
		L"ID3D12Device::CreateGraphicsPipelineState failed!");

	//
	// PSO for transparent objects
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC transparentPipelineStateDesc = opaquePipelineStateDesc;

	D3D12_RENDER_TARGET_BLEND_DESC transparentBlendDesc{};

	transparentBlendDesc.BlendEnable = true;
	transparentBlendDesc.LogicOpEnable = false;
	transparentBlendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
	transparentBlendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	transparentBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;
	// 参考https://github.com/ocornut/imgui/pull/3447
	transparentBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
	transparentBlendDesc.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
	transparentBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	transparentBlendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
	transparentBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	transparentPipelineStateDesc.BlendState.RenderTarget[0] = transparentBlendDesc;

	DXCheck(device->CreateGraphicsPipelineState(&transparentPipelineStateDesc,
		IID_PPV_ARGS(&pipelineStates["Transparent"])),
		L"ID3D12Device::CreateGraphicsPipelineState failed!");

	//
	// PSO for alpha tested objects
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC alphaTestedPsoDesc = opaquePipelineStateDesc;

	alphaTestedPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(shaders["AlphaTestedPS"]->GetBufferPointer()),
								shaders["AlphaTestedPS"]->GetBufferSize()
	};

	alphaTestedPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

	DXCheck(device->CreateGraphicsPipelineState(&alphaTestedPsoDesc, IID_PPV_ARGS(&pipelineStates["AlphaTested"])),
		L"ID3D12Device::CreateGraphicsPipelineState failed!");

	opaquePipelineStateDesc.pRootSignature = renderTextureRootSignature.Get();

	DXCheck(device->CreateGraphicsPipelineState(&opaquePipelineStateDesc,
		IID_PPV_ARGS(&renderTextureGraphicsPipelineState)),
		L"ID3D12Device::CreateGraphicsPipelineState failed!");

	D3D12_GRAPHICS_PIPELINE_STATE_DESC wireframePipelineStateDesc = opaquePipelineStateDesc;
	wireframePipelineStateDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;

	DXCheck(device->CreateGraphicsPipelineState(&wireframePipelineStateDesc,
		IID_PPV_ARGS(&pipelineStates["Wireframe"])),
		L"ID3D12Device::CreateGraphicsPipelineState failed!");

	// 用于标记模板缓冲区中镜面部分的PSO
	// 禁止对渲染目标的写操作
	CD3DX12_BLEND_DESC mirrorBlendState(D3D12_DEFAULT);
	mirrorBlendState.RenderTarget[0].RenderTargetWriteMask = 0;

	D3D12_DEPTH_STENCIL_DESC mirrorDepthStencilDesc{};
	mirrorDepthStencilDesc.DepthEnable = true;
	mirrorDepthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	mirrorDepthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	mirrorDepthStencilDesc.StencilEnable = true;
	mirrorDepthStencilDesc.StencilReadMask = 0xff;
	mirrorDepthStencilDesc.StencilWriteMask = 0xff;

	mirrorDepthStencilDesc.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	mirrorDepthStencilDesc.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	mirrorDepthStencilDesc.FrontFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
	mirrorDepthStencilDesc.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;

	// 我们不渲染背面朝向的多边形，因而对这些参数的设置并不关心
	mirrorDepthStencilDesc.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	mirrorDepthStencilDesc.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	mirrorDepthStencilDesc.BackFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
	mirrorDepthStencilDesc.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC markMirrorsPipelineStateDesc = opaquePipelineStateDesc;
	markMirrorsPipelineStateDesc.BlendState = mirrorBlendState;
	markMirrorsPipelineStateDesc.DepthStencilState = mirrorDepthStencilDesc;

	DXCheck(device->CreateGraphicsPipelineState(&markMirrorsPipelineStateDesc,
		IID_PPV_ARGS(&pipelineStates["MarkStencilMirrors"])),
		L"ID3D12Device::CreateGraphicsPipelineState failed!");

	// 用于渲染模板缓冲区中反射镜像的PSO
	D3D12_DEPTH_STENCIL_DESC reflectionsDepthStencilDesc{};
	reflectionsDepthStencilDesc.DepthEnable = true;
	reflectionsDepthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	reflectionsDepthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	reflectionsDepthStencilDesc.StencilEnable = true;
	reflectionsDepthStencilDesc.StencilReadMask = 0xff;
	reflectionsDepthStencilDesc.StencilWriteMask = 0xff;

	reflectionsDepthStencilDesc.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	reflectionsDepthStencilDesc.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	reflectionsDepthStencilDesc.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
	reflectionsDepthStencilDesc.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;

	// 我们不渲染背面朝向的多边形，因而对这些参数的设置并不关心
	reflectionsDepthStencilDesc.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	reflectionsDepthStencilDesc.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	reflectionsDepthStencilDesc.BackFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
	reflectionsDepthStencilDesc.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC drawReflectionsPSODesc = opaquePipelineStateDesc;
	drawReflectionsPSODesc.DepthStencilState = reflectionsDepthStencilDesc;
	drawReflectionsPSODesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
	drawReflectionsPSODesc.RasterizerState.FrontCounterClockwise = false;

	DXCheck(device->CreateGraphicsPipelineState(&drawReflectionsPSODesc,
		IID_PPV_ARGS(&pipelineStates["DrawStencilReflections"])),
		L"ID3D12Device::CreateGraphicsPipelineState failed!");

	D3D12_GRAPHICS_PIPELINE_STATE_DESC noAlphaPsoDesc = opaquePipelineStateDesc;

	noAlphaPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(shaders["NoAlphaPS"]->GetBufferPointer()),
								shaders["NoAlphaPS"]->GetBufferSize()
	};

	DXCheck(device->CreateGraphicsPipelineState(&noAlphaPsoDesc,
		IID_PPV_ARGS(&pipelineStates["NoAlpha"])),
		L"ID3D12Device::CreateGraphicsPipelineState failed!");

	//
	// PSO for shadow objects
	//

	// We are going to draw shadows with transparency, so base it off the transparency description.
	D3D12_DEPTH_STENCIL_DESC shadowDepthStencilDesc{};
	shadowDepthStencilDesc.DepthEnable = true;
	shadowDepthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	shadowDepthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	shadowDepthStencilDesc.StencilEnable = true;
	shadowDepthStencilDesc.StencilReadMask = 0xff;
	shadowDepthStencilDesc.StencilWriteMask = 0xff;

	shadowDepthStencilDesc.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	shadowDepthStencilDesc.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	shadowDepthStencilDesc.FrontFace.StencilPassOp = D3D12_STENCIL_OP_INCR;
	shadowDepthStencilDesc.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;

	// We are not rendering backfacing polygons, so these settings do not matter.
	shadowDepthStencilDesc.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	shadowDepthStencilDesc.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	shadowDepthStencilDesc.BackFace.StencilPassOp = D3D12_STENCIL_OP_INCR;
	shadowDepthStencilDesc.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC shadowPsoDesc = transparentPipelineStateDesc;
	shadowPsoDesc.DepthStencilState = shadowDepthStencilDesc;
	DXCheck(device->CreateGraphicsPipelineState(&shadowPsoDesc, IID_PPV_ARGS(&pipelineStates["Shadow"])),
		L"ID3D12Device::CreateGraphicsPipelineState failed!");

	//
	// PSO for tree sprites
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC treeSpritePsoDesc = opaquePipelineStateDesc;
	treeSpritePsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(shaders["TreeSpriteVS"]->GetBufferPointer()),
		shaders["TreeSpriteVS"]->GetBufferSize()
	};

	treeSpritePsoDesc.GS =
	{
		reinterpret_cast<BYTE*>(shaders["TreeSpriteGS"]->GetBufferPointer()),
		shaders["TreeSpriteGS"]->GetBufferSize()
	};

	treeSpritePsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(shaders["TreeSpritePS"]->GetBufferPointer()),
		shaders["TreeSpritePS"]->GetBufferSize()
	};

	treeSpritePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
	treeSpritePsoDesc.InputLayout = { treeSpriteInputLayout.data(), static_cast<uint32_t>(treeSpriteInputLayout.size()) };
	treeSpritePsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

	DXCheck(device->CreateGraphicsPipelineState(&treeSpritePsoDesc, IID_PPV_ARGS(&pipelineStates["TreeSprites"])),
		L"ID3D12Device::CreateGraphicsPipelineState failed!");
}

void D3DApp::createSkyboxGraphicsPipelineState()
{
	// 创建天空盒的PSO, 注意天空盒子不需要深度测试
	D3D12_GRAPHICS_PIPELINE_STATE_DESC skyboxGraphicsPipelineStateDesc{};
	skyboxGraphicsPipelineStateDesc.DepthStencilState.DepthEnable = FALSE;
	skyboxGraphicsPipelineStateDesc.DepthStencilState.StencilEnable = FALSE;
	skyboxGraphicsPipelineStateDesc.InputLayout = { skyboxInputLayout.data(), static_cast<uint32_t>(skyboxInputLayout.size()) };
	skyboxGraphicsPipelineStateDesc.pRootSignature = rootSignature.Get();
	//skyboxGraphicsPipelineStateDesc.VS = { skyboxVertexShader->GetBufferPointer(), skyboxVertexShader->GetBufferSize() };
	//skyboxGraphicsPipelineStateDesc.PS = { skyboxPixelShader->GetBufferPointer(), skyboxPixelShader->GetBufferSize() };
	skyboxGraphicsPipelineStateDesc.VS = { shaders["SkyboxVS"]->GetBufferPointer(), shaders["SkyboxVS"]->GetBufferSize() };
	skyboxGraphicsPipelineStateDesc.PS = { shaders["SkyboxPS"]->GetBufferPointer(), shaders["SkyboxPS"]->GetBufferSize() };

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
	skyboxGraphicsPipelineStateDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	skyboxGraphicsPipelineStateDesc.RasterizerState.FrontCounterClockwise = FALSE;

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
	skyboxGraphicsPipelineStateDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	//skyboxGraphicsPipelineStateDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	//skyboxGraphicsPipelineStateDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	skyboxGraphicsPipelineStateDesc.SampleMask = UINT_MAX;
	skyboxGraphicsPipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	skyboxGraphicsPipelineStateDesc.NumRenderTargets = 1;
	skyboxGraphicsPipelineStateDesc.RTVFormats[0] = swapChainFormat;
	skyboxGraphicsPipelineStateDesc.SampleDesc.Count = 1;

	DXCheck(device->CreateGraphicsPipelineState(&skyboxGraphicsPipelineStateDesc, IID_PPV_ARGS(&skyboxGraphicsPipelineState)), L"Create skybox pipeline state object failed!");
}

void D3DApp::createCommandLists()
{
	// 创建直接命令列表、捆绑包
	// 创建直接命令列表，在其上可以执行几乎所有的引擎命令(3D图形引擎、计算引擎、复制引擎等)
	// 注意初始时并没有使用PSO对象，此时其实这个命令列表依然可以记录命令
	createCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocatorDirectPre);

	createCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocatorDirectPre, pipelineStates["Opaque"].Get(), commandListDirectPre);

	createCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocatorDirectPost);

	createCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocatorDirectPost, pipelineStates["Opaque"].Get(), commandListDirectPost);

	createCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocatorDirectImGui);

	createCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocatorDirectImGui, pipelineStates["Opaque"].Get(), commandListDirectImGui);

	createCommandAllocator(D3D12_COMMAND_LIST_TYPE_BUNDLE, commandAllocatorSkybox);

	createCommandList(D3D12_COMMAND_LIST_TYPE_BUNDLE, commandAllocatorSkybox, skyboxGraphicsPipelineState, skyboxBundle);

	createCommandAllocator(D3D12_COMMAND_LIST_TYPE_BUNDLE, commandAllocatorScene);

	createCommandList(D3D12_COMMAND_LIST_TYPE_BUNDLE, commandAllocatorScene, pipelineStates["Opaque"].Get(), sceneBundle);

	createCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocatorRenderTextureObject);

	createCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocatorRenderTextureObject, pipelineStates["Opaque"].Get(), renderTextureObject);
}

void D3DApp::createFence()
{
	DXCheck(device->CreateFence(0,
		D3D12_FENCE_FLAG_NONE,
		IID_PPV_ARGS(&fence)),
		L"ID3D12Device::CreateFence failed!");

	fenceEvent = CreateEventEx(nullptr, FALSE, FALSE, EVENT_ALL_ACCESS);
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

	DXCheck(device->CreateHeap(&uploadHeapDesc, IID_PPV_ARGS(&uploadHeap)), L"ID3D12Device::CreateHealp failed!");

	uploadHeap->SetName(L"UploadHeap");
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
}

void D3DApp::loadResources()
{
	createTexture("Kanna", L"Assets/Textures/Kanna.jpg");
	createTexture("Bunny", L"Assets/Textures/Bunny.jpg");
	createTexture("CartoonGrass", L"Assets/Textures/CartoonGrass.jpg");
	createTexture("Earth", L"Assets/Textures/2k_earth_daymap.jpg");
	createTexture("Ocean", L"Assets/Textures/Ocean.jpg");
	createTexture("Mirror", L"Assets/Textures/Mirror.jpg");
	createTexture("Marry", L"Assets/Textures/MC003_Kozakura_Mari.jpg");

	loadDDSTexture("Skybox", L"Assets/Textures/Day_1024.dds", TextureDimension::TextureCube);
	loadDDSTexture("WireFence", L"Assets/Textures/WireFence.dds");
	loadDDSTexture("Ice", L"Assets/Textures/Ice.dds");
	loadDDSTexture("Ground", L"Assets/Textures/checkboard.dds");
	loadDDSTexture("Default", L"Assets/Textures/white1x1.dds");
	loadDDSTexture("Tree", L"Assets/Textures/TreeArray.dds", TextureDimension::TextureArray);

	textureCount = static_cast<uint32_t>(textures.size());
	
	loadCubeResource();

	loadModels();

	mainPassConstants.lights[1].direction = { -0.57735f, -0.57735f, 0.57735f };
	mainPassConstants.lights[1].strength = { 0.3f, 0.3f, 0.3f };
	mainPassConstants.lights[2].direction = { 0.0f, -0.707f, -0.707f };
	mainPassConstants.lights[2].strength = { 0.15f, 0.15f, 0.15f };
	mainPassConstants.lights[3].direction = { 0.0f, 0.0f, -1.0f };
	mainPassConstants.lights[3].strength = { 0.15f, 0.15f, 0.15f };

	// 执行命令列表并等待纹理资源上传完成，这一步是必须的
	DXCheck(commandListDirectPre->Close(), L"ID3D12GraphicsCommandList::Close failed!");
	ID3D12CommandList* commandLists[] = { commandListDirectPre.Get() };
	commandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);

	flushCommandQueue();

	//命令分配器先Reset一下，刚才已经执行过了一个复制纹理的命令
	DXCheck(commandAllocatorDirectPre->Reset(), L"ID3D12CommandAllocator::Reset failed!");

	//Reset命令列表，并重新指定命令分配器和PSO对象
	DXCheck(commandListDirectPre->Reset(commandAllocatorDirectPre.Get(), pipelineStates["Opaque"].Get()), L"ID3D12GraphicsCommandList::Reset failed!");
}

void D3DApp::loadModels()
{
	cube.load("Assets/Models/cube.obj", "Cube");
	bunny.load("Assets/Models/texture_bunny.obj", "Bunny");
	skybox.load("Assets/Models/cube.obj", "Cube");
	marry.load("Assets/Models/Marry.obj", "Marry");

	vertexCount += static_cast<uint32_t>(bunny.mesh.vertices.size());
	vertexCount += static_cast<uint32_t>(cube.mesh.vertices.size());
	vertexCount += static_cast<uint32_t>(cube.mesh.vertices.size());
	vertexCount += static_cast<uint32_t>(marry.mesh.vertices.size());
	indexCount += static_cast<uint32_t>(bunny.mesh.indices.size());
	indexCount += static_cast<uint32_t>(cube.mesh.indices.size());
	indexCount += static_cast<uint32_t>(marry.mesh.indices.size());

	vertexBufferSize = bunny.mesh.vertexBufferSize;

	indexBufferSize = bunny.mesh.indexBufferSize;

	skyboxIndexCount = static_cast<uint32_t>(skybox.mesh.indices.size());

	auto meshGeometry = createMeshGeometry(bunny);

	meshGeometries[meshGeometry->name] = std::move(meshGeometry);

	meshGeometry = createMeshGeometry(cube);

	meshGeometries[meshGeometry->name] = std::move(meshGeometry);

	cube.name = "WireFenceCube";

	meshGeometry = createMeshGeometry(cube);

	meshGeometries[meshGeometry->name] = std::move(meshGeometry);

	meshGeometry = createMeshGeometry(marry);

	meshGeometries[meshGeometry->name] = std::move(meshGeometry);

	GeometryGenerator geometryGenerator;
	auto land = geometryGenerator.createGrid(160.0f, 160.0f, 50, 50, 10.0f);

	for (auto& vertex : land.vertices)
	{
		vertex.position.y = getHillsHeight(vertex.position.x, vertex.position.z);
	}

	createMeshDataGeometry(land, "Land");

	vertexCount += static_cast<uint32_t>(land.vertices.size());
	indexCount += static_cast<uint32_t>(land.indices32.size());

	waves = std::make_unique<Waves>(128, 128, 1.0f, 0.03f, 4.0f, 0.2f);

	createOceanMeshGeometry();

	createTreeSpritesGeometry(20);

	auto sphere = geometryGenerator.createSphere(1.0f, 32, 32);
	createMeshDataGeometry(sphere, "Sphere");

	vertexCount += static_cast<uint32_t>(sphere.vertices.size());
	indexCount += static_cast<uint32_t>(sphere.indices32.size());

	auto mirror = geometryGenerator.createQuad(5.0f, 5.0f);
	createMeshDataGeometry(mirror, "Mirror");

	vertexCount += static_cast<uint32_t>(mirror.vertices.size());
	indexCount += static_cast<uint32_t>(mirror.indices32.size());

	triangleCount = indexCount / 3;
}

void D3DApp::createTexture(std::unique_ptr<Texture>& texture)
{
	auto imageData = loadImage(texture->path);
	//auto imageData = loadImage(L"Assets/Textures/Skrik.bmp");

	// 创建纹理的默认堆
	D3D12_HEAP_DESC textureHeapDesc{};

	// 为堆指定纹理图片至少2倍大小的空间，这里没有详细取计算了，只是指定了一个足够大的空间，够放纹理就行
	// 实际应用中也是要综合考虑分配堆的大小，以便可以重用堆
	textureHeapDesc.SizeInBytes = ROUND_UP(2 * imageData.rowPitch * imageData.width, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

	// 指定堆的对齐方式，这里使用了默认的64K边界对齐，因为我们暂时不需要MSAA支持
	textureHeapDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
	textureHeapDesc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
	textureHeapDesc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	textureHeapDesc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

	// 拒绝渲染目标纹理，拒绝深度模板纹理，实际就只是用来摆放普通纹理
	textureHeapDesc.Flags = D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES | D3D12_HEAP_FLAG_DENY_BUFFERS;

	DXCheck(device->CreateHeap(&textureHeapDesc, IID_PPV_ARGS(&textureHeap)), L"ID3D12Device::CreateHealp failed!");

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
	// 同时可以在这个堆上反复调用CreatePlacedResource来创建不同的纹理，当然前提是它们不再被使用的时候，才考虑重用堆
	DXCheck(device->CreatePlacedResource(textureHeap.Get(),
		0,
		&textureDesc,	// 可以使用CD3DX12_RESOURCE_DESC::Tex2D来简化结构体的初始化
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&texture->resource)), L"ID3D12Device::CreatePlacedResource failed!");

	texture->resource->SetName(L"Texture");

	// 获取上传堆资源缓冲的大小，这个尺寸通常大于实际图片的尺寸
	auto resourceRequiredSize = GetRequiredIntermediateSize(texture->resource.Get(), 0, 1);

	// 追踪占用空间最大的纹理尺寸，否则后加载的纹理比先加载的纹理尺寸小的情况下，会导致错误的计算HeapOffset
	// 导致后面创建的资源覆盖掉纹理数据，引发[ STATE_CREATION WARNING #926: HEAP_ADDRESS_RANGE_INTERSECTS_MULTIPLE_BUFFERS]
	if (resourceRequiredSize > textureUploadBufferSize)
	{
		textureUploadBufferSize = resourceRequiredSize;
	}

	// 使用“定位方式”创建用于上传纹理数据的缓冲资源

	// 创建用于上传纹理的资源, 注意其类型是Buffer
	// 上传堆对于GPU访问来说性能是很差的，
	// 所以对于几乎不变的数据尤其像纹理都是
	// 通过它来上传至GPU访问更高效的默认堆中
	DXCheck(device->CreatePlacedResource(uploadHeap.Get(),
		0,
		&CD3DX12_RESOURCE_DESC::Buffer(textureUploadBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&texture->uploadHeap)), L"ID3D12Device::CreatePlacedResource failed!");

	texture->uploadHeap->SetName(L"textureUploadBuffer");

	// 按照资源缓冲大小来分配实际图片数据存储的内存大小
	void* textureData = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, textureUploadBufferSize);

	if (textureData == nullptr)
	{
		DXThrow(HRESULT_FROM_WIN32(GetLastError()), L"HeapAlloc failed!");
	}

	// 从图片中读取数据
	DXCheck(imageData.data->CopyPixels(
		nullptr,
		imageData.rowPitch,
		//注意这里才是图片数据真实的大小，这个值通常小于缓冲的大小
		static_cast<uint32_t>(imageData.rowPitch * imageData.width),
		reinterpret_cast<uint8_t*>(textureData)), L"IWICBitmapSource::CopyPixels failed!");

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

	D3D12_RESOURCE_DESC resourceDesc = texture->resource->GetDesc();

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

	DXCheck(texture->uploadHeap->Map(0, nullptr, reinterpret_cast<void**>(&data)), L"ID3D12Resource::Map failed!");

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
	texture->uploadHeap->Unmap(0, nullptr);

	// 释放图片数据，做一个干净的程序员
	HeapFree(GetProcessHeap(), 0, textureData);

	// 向直接命令列表发出从上传堆复制纹理数据到默认堆的命令，
	// 执行同步并等待，即完成第二个copy动作，由GPU上的复制引擎完成
	// 注意此时直接命令列表还没有绑定PSO对象，因此它也是不能执行3D图形命令的
	// 但是可以执行复制命令，因为复制引擎不需要什么额外的状态设置之类的参数
	CD3DX12_TEXTURE_COPY_LOCATION dest(texture->resource.Get(), 0);
	CD3DX12_TEXTURE_COPY_LOCATION src(texture->uploadHeap.Get(), textureLayout);

	commandListDirectPre->CopyTextureRegion(&dest, 0, 0, 0, &src, nullptr);

	// 设置一个资源屏障，同步并确认复制操作完成
	// 直接使用结构体然后调用的形式
	//D3D12_RESOURCE_BARRIER resourceBarrier{};
	//resourceBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	//resourceBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	//resourceBarrier.Transition.pResource = texture->resource.Get();
	//resourceBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	//resourceBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	//resourceBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

	// 或者使用D3DX12库中的工具类调用的等价形式，下面的方式更简洁一些
	// pIcommandListDirect->ResourceBarrier(1
	//	 , &CD3DX12_RESOURCE_BARRIER::Transition(pITexcute.Get()
	//	 , D3D12_RESOURCE_STATE_COPY_DEST
	//	 , D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
	// );

	//commandListDirectPre->ResourceBarrier(1, &resourceBarrier);

	resourceTransition(texture->resource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	executeCommandLists();

	flushCommandQueue();

	resetFrameResourceCommandAllocator();
}

void D3DApp::createTexture(const std::string& name, const std::wstring& path)
{
	auto texture = std::make_unique<Texture>();
	texture->name = name;
	texture->path = path;
	texture->index = static_cast<uint32_t>(textures.size());
	
	createTexture(texture);

	textures[texture->name] = std::move(texture);
}

void D3DApp::flushCommandQueue()
{
	fenceValue++;

	// Add an instruction to the command queue to set a new fence point.  Because we 
	// are on the GPU timeline, the new fence point won't be set until the GPU finishes
	// processing all the commands prior to this Signal().
	DXCheck(commandQueue->Signal(fence.Get(), fenceValue), L"ID3D12CommandQueue::Signal failed!");

	// 看命令有没有真正执行到围栏标记的这里，没有就利用事件去等待，注意使用的是命令队列对象的指针
	// Wait until the GPU has completed commands up to this fence point.
	if (fence->GetCompletedValue() < fenceValue)
	{
		auto fenceEvent = CreateEventEx(nullptr, FALSE, FALSE, EVENT_ALL_ACCESS);

		if (fenceEvent == nullptr)
		{
			DXCheck(HRESULT_FROM_WIN32(GetLastError()), L"CreateEvent failed!");
		}

		// Fire event when GPU hits current fence.  
		DXCheck(fence->SetEventOnCompletion(fenceValue, fenceEvent), L"ID3D12Fence::SetEventOnCompletion failed!");
		WaitForSingleObject(fenceEvent, INFINITE);
		CloseHandle(fenceEvent);
	}
}

void D3DApp::loadDDSTexture(const std::string& name, const std::wstring& path, TextureDimension dimension)
{
	//加载Skybox的 Cube Map 需要的变量
	std::unique_ptr<uint8_t[]> ddsData;
	std::vector<D3D12_SUBRESOURCE_DATA> subResources;
	DDS_ALPHA_MODE alphaMode = DDS_ALPHA_MODE_UNKNOWN;
	bool bIsCube = false;

	ID3D12Resource* textureResource;

	DXCheck(LoadDDSTextureFromFile(
		device.Get(),
		path.c_str(),
		&textureResource,
		ddsData,
		subResources,
		SIZE_MAX,
		&alphaMode,
		&bIsCube
		), L"LoadDDSTextureFromFile failed!");

	// 上面函数加载的纹理在隐式默认堆上，两个Copy动作需要我们自己完成
	textureResource->SetName(utf8_decode(name).c_str());

	// 获取skybox的资源大小，并创建上传堆
	skyboxTextureUploadBufferSize = GetRequiredIntermediateSize(textureResource, 0, static_cast<uint32_t>(subResources.size()));

	if (skyboxUploadHeap == nullptr)
	{
		D3D12_HEAP_DESC uploadHeapDesc{};
		uploadHeapDesc.Alignment = 0;
		uploadHeapDesc.SizeInBytes = ROUND_UP(10 * skyboxTextureUploadBufferSize, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
		uploadHeapDesc.Properties.Type = D3D12_HEAP_TYPE_UPLOAD;
		uploadHeapDesc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		uploadHeapDesc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

		// 上传堆就是缓冲，可以摆放任意数据
		uploadHeapDesc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;

		DXCheck(device->CreateHeap(&uploadHeapDesc, IID_PPV_ARGS(&skyboxUploadHeap)), L"Create skybox upload heap failed!");

		setD3D12DebugNameComPtr(skyboxUploadHeap);
	}

	// 在skybox上传堆中创建用于上传纹理数据的缓冲资源
	DXCheck(device->CreatePlacedResource(
		skyboxUploadHeap.Get(),
		0,
		&CD3DX12_RESOURCE_DESC::Buffer(skyboxTextureUploadBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&skyboxTextureUploadBuffer)), L"Create skybox texture upload buffer failed!");

	skyboxTextureUploadBuffer->SetName(L"skyboxTextureUploadBuffer");

	// 上传skybox的纹理
	UpdateSubresources(
		commandListDirectPre.Get(),
		textureResource,
		skyboxTextureUploadBuffer.Get(),
		0,
		0,
		static_cast<uint32_t>(subResources.size()),
		subResources.data()
	);

	resourceTransition(textureResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	executeCommandLists();

	flushCommandQueue();

	resetFrameResourceCommandAllocator();

	auto texture = std::make_unique<Texture>();
	texture->resource = textureResource;
	texture->index = static_cast<uint32_t>(textures.size());
	texture->dimension = dimension;

	textures[name] = std::move(texture);
}

void D3DApp::createTextureShaderResourceView(const std::unique_ptr<Texture>& texture, uint32_t index)
{
	createShaderResourceView(texture->dimension, texture->resource.Get(), shaderResourceViewDescriptorHeap, index);
}

void D3DApp::createTextureArrayShaderResourceView(const std::unique_ptr<Texture>& texture, uint32_t index /*= 0*/)
{

}

void D3DApp::createTextureShaderResourceViews()
{
	uint32_t index = 0;
	for (const auto& e : textures)
	{
		createTextureShaderResourceView(e.second, e.second->index);
	}
}

// 创建RTV(渲染目标视图)描述符堆(用于创建renderTexture)
void D3DApp::createRenderTextureRTVDescriptorHeap()
{
	createDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false, renderTextureRTVDescriptorHeap, L"renderTextureRTVDescriptorHeap");

	setD3D12DebugNameComPtr(renderTextureRTVDescriptorHeap);
}

// 创建一张能作为渲染目标的纹理(指定D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET标志)
void D3DApp::createRenderTextureRTV(uint32_t width, uint32_t height)
{
	D3D12_RESOURCE_DESC renderTextureDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		swapChainFormat,
		width,
		height,
		1,
		1,
		1,
		0,
		D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

	D3D12_CLEAR_VALUE clearValue{};
	clearValue.Format = swapChainFormat;
	memcpy_s(clearValue.Color, sizeof(ImVec4), &clearColor, sizeof(ImVec4));

	DXCheck(device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&renderTextureDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
						 D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
		&clearValue,
		IID_PPV_ARGS(&renderTexture)), L"Create render texture failed!");

	setD3D12DebugNameComPtr(renderTexture);

	device->CreateRenderTargetView(renderTexture.Get(), nullptr,
		CPU_DESCRIPTOR_HEAP_START(renderTextureRTVDescriptorHeap));
}

// 除了创建RTV所需的描述符堆，还需要将renderTexture作为SRV使用的描述符堆
void D3DApp::createRenderTextureSRVDescriptorHeap()
{
	createDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 
			2, // 1 SRV + 1 CBV
				   true, 
			renderTextureSRVDescriptorHeap, 
			  ObjectName(renderTextureSRVDescriptorHeap));
}

// 为renderTexture创建SRV
void D3DApp::createRenderTextureSRV()
{
	//createShaderResourceView(D3D12_SRV_DIMENSION_TEXTURE2D, renderTexture, renderTextureSRVDescriptorHeap);
	createShaderResourceView(TextureDimension::Texture2D, renderTexture, imGuiShaderResourceViewDescriptorHeap, 8);
}

// 为renderTexture的RTV创建配套的DSV描述符堆
void D3DApp::createRenderTextureDSVDescriptorHeap()
{
	createDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 
		    1, 
				   false, 
			          renderTextureDSVDescriptorHeap, 
						 ObjectName(renderTextureDSVDescriptorHeap));
}

// 为renderTexture的RTV创建配套的DSV
void D3DApp::createRenderTextureDSV(uint32_t width, uint32_t height)
{
	createDepthStencilView(width, height, depthStencilFormat, renderTextureDSVDescriptorHeap, renderTextureDepthStencilBuffer);
}

void D3DApp::createVertexBuffer(const DXModel& model, const ComPtr<ID3D12Heap>& heap, uint64_t offset, ComPtr<ID3D12Resource>& vertexBuffer, D3D12_VERTEX_BUFFER_VIEW& vertexBufferView)
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
	vertexBufferSize = model.mesh.vertexBufferSize;

	// 使用“定位方式”创建顶点缓冲和索引缓冲，使用与上传纹理数据缓冲相同的上传堆，注意第二个参数指出了堆中的偏移位置
	// 按照堆边界对齐要求，我们主动将偏移位置对齐到了64k的边界上
	DXCheck(device->CreatePlacedResource(
		heap.Get(),
		offset,
		&CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(vertexBuffer.GetAddressOf())),
		L"Create vertex buffer failed!");

	// 使用 map-memcpy-unmap 大法将数据传至顶点缓冲对象
	// 注意顶点缓冲使用的是和上传纹理数据缓冲相同的一个堆，这很神奇
	UINT8* vertexDataBegin = nullptr;

	CD3DX12_RANGE readRange(0, 0);
	DXCheck(vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&vertexDataBegin)), L"ID3D12Resource::Map failed!");

	memcpy_s(vertexDataBegin, vertexBufferSize, model.mesh.vertices.data(), vertexBufferSize);

	vertexBuffer->Unmap(0, nullptr);

	vertexBufferView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
	vertexBufferView.StrideInBytes = sizeof(DXVertex);
	vertexBufferView.SizeInBytes = vertexBufferSize;
}

void D3DApp::createVertexBuffer(const DXModel& model)
{
	// 计算Vertex Buffer在上传堆中的偏移值(上传堆有的大小至少是2 * uploadBufferSize，前一个uploadBufferSize的空间在前面用于上传纹理)
	bufferOffset = ROUND_UP(textureUploadBufferSize, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
	//vertexBufferSize = model.mesh.vertexBufferSize;

	createVertexBuffer(model, uploadHeap, bufferOffset, vertexBuffer, vertexBufferView);
}

void D3DApp::createIndexBuffer(const DXModel& model, const ComPtr<ID3D12Heap>& heap, uint64_t offset, ComPtr<ID3D12Resource>& indexBuffer, D3D12_INDEX_BUFFER_VIEW& indexBufferView)
{
	// 计算边界对齐的正确的偏移位置
	bufferOffset = ROUND_UP(bufferOffset + vertexBufferSize, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
	indexBufferSize = model.mesh.indexBufferSize;

	DXCheck(device->CreatePlacedResource(
		heap.Get(),
		offset,
		&CD3DX12_RESOURCE_DESC::Buffer(indexBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(indexBuffer.GetAddressOf())), L"Create index buffer failed!");

	uint8_t* indexDataBegin = nullptr;

	CD3DX12_RANGE readRange(0, 0);
	DXCheck(indexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&indexDataBegin)), L"ID3D12Resource::Map failed!");
	memcpy_s(indexDataBegin, indexBufferSize, model.mesh.indices.data(), indexBufferSize);
	indexBuffer->Unmap(0, nullptr);

	indexBufferView.BufferLocation = indexBuffer->GetGPUVirtualAddress();
	indexBufferView.Format = DXGI_FORMAT_R32_UINT;
	indexBufferView.SizeInBytes = indexBufferSize;
}

void D3DApp::createIndexBuffer(const DXModel& model)
{
	// 计算边界对齐的正确的偏移位置
	bufferOffset = ROUND_UP(bufferOffset + vertexBufferSize, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
	indexBufferSize = model.mesh.indexBufferSize;

	createIndexBuffer(model, uploadHeap, bufferOffset, indexBuffer, indexBufferView);
}

void D3DApp::createSkyboxVertexBuffer(const DXModel& model)
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
	skyboxBufferOffset = ROUND_UP(skyboxTextureUploadBufferSize, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

	skyboxVertexBufferSize = model.mesh.vertexBufferSize;

	// 使用“定位方式”创建顶点缓冲和索引缓冲，使用与上传纹理数据缓冲相同的上传堆，注意第二个参数指出了堆中的偏移位置
	// 按照堆边界对齐要求，我们主动将偏移位置对齐到了64k的边界上
	DXCheck(device->CreatePlacedResource(
		skyboxUploadHeap.Get(),
		skyboxBufferOffset,
		&CD3DX12_RESOURCE_DESC::Buffer(skyboxVertexBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&skyboxVertexBuffer)),
		L"Create vertex buffer failed!");

	skyboxVertexBuffer->SetName(L"skyboxVertexBuffer");

	// 使用 map-memcpy-unmap 大法将数据传至顶点缓冲对象
	// 注意顶点缓冲使用的是和上传纹理数据缓冲相同的一个堆，这很神奇
	UINT8* vertexDataBegin = nullptr;

	CD3DX12_RANGE readRange(0, 0);
	DXCheck(skyboxVertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&vertexDataBegin)), L"ID3D12Resource::Map failed!");

	memcpy_s(vertexDataBegin, skyboxVertexBufferSize, model.mesh.vertices.data(), skyboxVertexBufferSize);

	skyboxVertexBuffer->Unmap(0, nullptr);

	skyboxVertexBufferView.BufferLocation = skyboxVertexBuffer->GetGPUVirtualAddress();
	skyboxVertexBufferView.StrideInBytes = sizeof(DXVertex);
	skyboxVertexBufferView.SizeInBytes = skyboxVertexBufferSize;
}

void D3DApp::createSkyboxIndexBuffer(const DXModel& model)
{
	// 计算边界对齐的正确的偏移位置
	skyboxBufferOffset = ROUND_UP(skyboxBufferOffset + skyboxVertexBufferSize, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
	skyboxIndexBufferSize = model.mesh.indexBufferSize;

	DXCheck(device->CreatePlacedResource(
		skyboxUploadHeap.Get(),
		skyboxBufferOffset,
		&CD3DX12_RESOURCE_DESC::Buffer(skyboxIndexBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&skyboxIndexBuffer)), L"Create index buffer failed!");

	skyboxIndexBuffer->SetName(L"skyboxIndexBuffer");

	uint8_t* indexDataBegin = nullptr;

	CD3DX12_RANGE readRange(0, 0);
	DXCheck(skyboxIndexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&indexDataBegin)), L"ID3D12Resource::Map failed!");
	memcpy_s(indexDataBegin, skyboxIndexBufferSize, model.mesh.indices.data(), skyboxIndexBufferSize);
	skyboxIndexBuffer->Unmap(0, nullptr);

	skyboxIndexBufferView.BufferLocation = skyboxIndexBuffer->GetGPUVirtualAddress();
	skyboxIndexBufferView.Format = DXGI_FORMAT_R32_UINT;
	skyboxIndexBufferView.SizeInBytes = skyboxIndexBufferSize;
}

void D3DApp::createConstantBufferView(const ComPtr<ID3D12Heap>& heap, 
									  uint64_t offset, uint32_t size, 
									  const ComPtr<ID3D12DescriptorHeap>& descriptorHeap,
									  uint32_t index,
									  ComPtr<ID3D12Resource>& constantBuffer,
									  ObjectConstants** objectConstantBuffer)
{
	// 以“定位方式”创建常量缓冲
	bufferOffset = ROUND_UP(bufferOffset + indexBufferSize, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

	constantBufferSize = ROUND_UP(sizeof(ObjectConstants), 256) * 256;

	// 创建常量缓冲区，注意缓冲区尺寸设置为256边界对齐大小
	DXCheck(device->CreatePlacedResource(
		heap.Get(),
		offset,
		&CD3DX12_RESOURCE_DESC::Buffer(size),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(constantBuffer.GetAddressOf())),
		L"Create constant buffer failed!");

	constantBuffer->SetName(L"constantBuffer");

	// Map之后就不再Unmap了，直接复制数据进去，这样每帧都不用 map-copy-unmap 浪费时间了
	DXCheck(constantBuffer->Map(0, nullptr, reinterpret_cast<void**>(objectConstantBuffer)), L"ID3D12Resource::Map failed!");

	D3D12_CONSTANT_BUFFER_VIEW_DESC constantBufferViewDesc{};
	constantBufferViewDesc.BufferLocation = constantBuffer->GetGPUVirtualAddress();
	constantBufferViewDesc.SizeInBytes = size;

	CD3DX12_CPU_DESCRIPTOR_HANDLE constantBufferViewHandle(
		CPU_DESCRIPTOR_HEAP_START(descriptorHeap),
		index,
		SRVCBVUAVDescriptorSize);

	device->CreateConstantBufferView(&constantBufferViewDesc, constantBufferViewHandle);
}

void D3DApp::createConstantBufferView()
{
	// 以“定位方式”创建常量缓冲
	bufferOffset = ROUND_UP(bufferOffset + indexBufferSize, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

	constantBufferSize = ROUND_UP(sizeof (ObjectConstants), 256);

	//// 创建常量缓冲区，注意缓冲区尺寸设置为256边界对齐大小
	//DXCheck(device->CreatePlacedResource(
	//	uploadHeap.Get(),
	//	bufferOffset,
	//	&CD3DX12_RESOURCE_DESC::Buffer(constantBufferSize),
	//	D3D12_RESOURCE_STATE_GENERIC_READ,
	//	nullptr,
	//	IID_PPV_ARGS(&constantBuffer)),
	//	L"Create constant buffer failed!");

	//// Map之后就不再Unmap了，直接复制数据进去，这样每帧都不用 map-copy-unmap 浪费时间了
	//DXCheck(constantBuffer->Map(0, nullptr, reinterpret_cast<void**>(&modelViewProjectionBuffer)), L"ID3D12Resource::Map failed!");

	//D3D12_CONSTANT_BUFFER_VIEW_DESC constantBufferViewDesc{};
	//constantBufferViewDesc.BufferLocation = constantBuffer->GetGPUVirtualAddress();
	//constantBufferViewDesc.SizeInBytes = constantBufferSize;

	//CD3DX12_CPU_DESCRIPTOR_HANDLE constantBufferViewHandle(shaderResourceViewDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
	//	1,
	//	device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));

	//device->CreateConstantBufferView(&constantBufferViewDesc, constantBufferViewHandle);

	createConstantBufferView(uploadHeap, bufferOffset, constantBufferSize, shaderResourceViewDescriptorHeap, textureCount, constantBuffer, &objectConstants);
}

void D3DApp::createSkyboxConstantBufferView()
{
	// 以“定位方式”创建常量缓冲
	skyboxBufferOffset = ROUND_UP(skyboxBufferOffset + skyboxIndexBufferSize, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

	skyboxConstantBufferSize = ROUND_UP(sizeof(ObjectConstants), 256);

	// 创建常量缓冲区，注意缓冲区尺寸设置为256边界对齐大小
	DXCheck(device->CreatePlacedResource(
		skyboxUploadHeap.Get(),
		skyboxBufferOffset,
		&CD3DX12_RESOURCE_DESC::Buffer(skyboxConstantBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&skyboxConstantBuffer)),
		L"Create skybox constant buffer failed!");

	skyboxConstantBuffer->SetName(L"skyboxConstantBuffer");

	// Map之后就不再Unmap了，直接复制数据进去，这样每帧都不用 map-copy-unmap 浪费时间了
	DXCheck(skyboxConstantBuffer->Map(0, nullptr, reinterpret_cast<void**>(&skyboxConstants)), L"ID3D12Resource::Map failed!");

	D3D12_CONSTANT_BUFFER_VIEW_DESC skyboxConstantBufferViewDesc{};
	skyboxConstantBufferViewDesc.BufferLocation = skyboxConstantBuffer->GetGPUVirtualAddress();
	skyboxConstantBufferViewDesc.SizeInBytes = skyboxConstantBufferSize;

	CD3DX12_CPU_DESCRIPTOR_HANDLE skyboxConstantBufferViewHandle(CPU_DESCRIPTOR_HEAP_START(skyboxDescriptorHeap),
		1,
		SRVCBVUAVDescriptorSize);

	device->CreateConstantBufferView(&skyboxConstantBufferViewDesc, skyboxConstantBufferViewHandle);

	// 以“定位方式”创建常量缓冲
	skyboxBufferOffset = ROUND_UP(skyboxBufferOffset + skyboxConstantBufferSize, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

	skyboxConstantBufferSize = ROUND_UP(sizeof(PassConstants), 256);

	// 创建常量缓冲区，注意缓冲区尺寸设置为256边界对齐大小
	DXCheck(device->CreatePlacedResource(
		skyboxUploadHeap.Get(),
		skyboxBufferOffset,
		&CD3DX12_RESOURCE_DESC::Buffer(skyboxConstantBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&skyboxPassConstantBuffer)),
		L"Create skybox constant buffer failed!");

	skyboxPassConstantBuffer->SetName(L"skyboxConstantBuffer");

	// Map之后就不再Unmap了，直接复制数据进去，这样每帧都不用 map-copy-unmap 浪费时间了
	DXCheck(skyboxPassConstantBuffer->Map(0, nullptr, reinterpret_cast<void**>(&skyboxPassConstants)), L"ID3D12Resource::Map failed!");

	D3D12_CONSTANT_BUFFER_VIEW_DESC skyboxPassConstantBufferViewDesc{};
	skyboxPassConstantBufferViewDesc.BufferLocation = skyboxPassConstantBuffer->GetGPUVirtualAddress();
	skyboxPassConstantBufferViewDesc.SizeInBytes = skyboxConstantBufferSize;

	skyboxConstantBufferViewHandle.Offset(1, SRVCBVUAVDescriptorSize);

	device->CreateConstantBufferView(&skyboxPassConstantBufferViewDesc, skyboxConstantBufferViewHandle);
}

void D3DApp::createRenderTextureConstantBufferView()
{
	bufferOffset = ROUND_UP(bufferOffset + constantBufferSize, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

	constantBufferSize = ROUND_UP(sizeof(ObjectConstants), 256);

	//createConstantBufferView(uploadHeap, bufferOffset, constantBufferSize, renderTextureSRVDescriptorHeap, 1, renderTextureConstantBuffer, &renderTextureModelViewProjectionBuffer);
	createConstantBufferView(uploadHeap, bufferOffset, constantBufferSize, imGuiShaderResourceViewDescriptorHeap, 9, renderTextureConstantBuffer, &renderTextureConstants);
}

void D3DApp::createDepthStencilDescriptorHeap()
{
	createDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
		    1, 
				   false, 
						depthStencilViewDescriptorHeap, 
						ObjectName(depthStencilViewDescriptorHeap));
}

void D3DApp::createDepthStencilView()
{
	createDepthStencilView(windowWidth, windowHeight, depthStencilFormat, depthStencilViewDescriptorHeap, depthStencilBuffer);
}

void D3DApp::createSamplerDescriptorHeap()
{
	createDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, sampleMaxCount, true, samplerDescriptorHeap, ObjectName(samplerDescriptorHeap));
}

void D3DApp::createSamplers()
{
	CD3DX12_CPU_DESCRIPTOR_HANDLE samplerDescriptorHeapHandle(CPU_DESCRIPTOR_HEAP_START(samplerDescriptorHeap));

	D3D12_SAMPLER_DESC samplerDesc{};
	samplerDesc.Filter = D3D12_FILTER_ANISOTROPIC;
	samplerDesc.MinLOD = 0;
	samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
	samplerDesc.MaxAnisotropy = 16;
	samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;

	// Sampler 1
	samplerDesc.BorderColor[0] = 1.0f;
	samplerDesc.BorderColor[1] = 0.0f;
	samplerDesc.BorderColor[2] = 1.0f;
	samplerDesc.BorderColor[3] = 1.0f;
	samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	device->CreateSampler(&samplerDesc, samplerDescriptorHeapHandle);

	samplerDescriptorHeapHandle.Offset(samplerDescriptorSize);

	// Sampler 2
	samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
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

void D3DApp::createSkyboxSamplerDescriptorHeap()
{
	createDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1, true, skyboxSamplerDescriptorHeap, ObjectName(skyboxSamplerDescriptorHeap));
}

void D3DApp::createSkyboxSampler()
{
	CD3DX12_CPU_DESCRIPTOR_HANDLE skyboxSamplerDescriptorHeapHandle(CPU_DESCRIPTOR_HEAP_START(skyboxSamplerDescriptorHeap));

	D3D12_SAMPLER_DESC skyboxSamplerDesc{};
	skyboxSamplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	skyboxSamplerDesc.MinLOD = 0;
	skyboxSamplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
	skyboxSamplerDesc.MaxAnisotropy = 1;
	skyboxSamplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;

	// Sampler 1
	skyboxSamplerDesc.BorderColor[0] = 1.0f;
	skyboxSamplerDesc.BorderColor[1] = 0.0f;
	skyboxSamplerDesc.BorderColor[2] = 1.0f;
	skyboxSamplerDesc.BorderColor[3] = 1.0f;
	skyboxSamplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	skyboxSamplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	skyboxSamplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	device->CreateSampler(&skyboxSamplerDesc, skyboxSamplerDescriptorHeapHandle);
}

void D3DApp::recordCommands()
{
	// 场景捆绑包
	sceneBundle->SetPipelineState(pipelineStates["Opaque"].Get());
	sceneBundle->SetGraphicsRootSignature(rootSignature.Get());

	// 设置要使用的描述符堆
	ID3D12DescriptorHeap* descriptorHeaps[] = { shaderResourceViewDescriptorHeap.Get(), samplerDescriptorHeap.Get() };
	sceneBundle->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	// SRV
	CD3DX12_GPU_DESCRIPTOR_HANDLE constantBufferViewHandle(GPU_DESCRIPTOR_HEAP_START(shaderResourceViewDescriptorHeap));

	sceneBundle->SetGraphicsRootDescriptorTable(0, GPU_DESCRIPTOR_HEAP_START(shaderResourceViewDescriptorHeap));

	// ObjectConstants
	constantBufferViewHandle.Offset(textureCount, SRVCBVUAVDescriptorSize);
	sceneBundle->SetGraphicsRootDescriptorTable(1, constantBufferViewHandle);

	CD3DX12_GPU_DESCRIPTOR_HANDLE samplerDescriptorHandle(GPU_DESCRIPTOR_HEAP_START(samplerDescriptorHeap), currentSamplerNo, samplerDescriptorSize);

	// Sampler
	sceneBundle->SetGraphicsRootDescriptorTable(4, samplerDescriptorHandle);

	// PassContants
	auto passConstantBufferViewIndex = passConstantBufferViewOffset + currentFrameResourceIndex;
	auto passConstantBufferViewHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(GPU_DESCRIPTOR_HEAP_START(shaderResourceViewDescriptorHeap));
	passConstantBufferViewHandle.Offset(passConstantBufferViewIndex, SRVCBVUAVDescriptorSize);

	sceneBundle->SetGraphicsRootDescriptorTable(3, passConstantBufferViewHandle);

	// 注意我们使用的渲染手法是三角形列表，也就是通常的Mesh网格
	sceneBundle->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	//sceneBundle->IASetVertexBuffers(0, 1, &vertexBufferView);
	//sceneBundle->IASetIndexBuffer(&indexBufferView);

	//sceneBundle->DrawIndexedInstanced(indexCount, 1, 0, 0, 0);

	//drawRenderItems(sceneBundle, opaqueRenderItems);

	sceneBundle->IASetVertexBuffers(0, 1, &meshGeometries["Cube"]->VertexBufferView());
	sceneBundle->IASetIndexBuffer(&meshGeometries["Cube"]->IndexBufferView());

	sceneBundle->DrawIndexedInstanced(meshGeometries["Cube"]->drawArgs["Cube"].indexCount, 1, 0, 0, 0);

	sceneBundle->Close();

	//// 渲染到纹理演示对象
	//renderTextureObjectBundle->SetPipelineState(renderTextureGraphicsPipelineState.Get());
	//renderTextureObjectBundle->SetGraphicsRootSignature(renderTextureRootSignature.Get());

	//// 设置要使用的描述符堆
	//ID3D12DescriptorHeap* renderTextureDescriptorHeaps[] = { renderTextureSRVDescriptorHeap.Get(), samplerDescriptorHeap.Get() };

	//renderTextureObjectBundle->SetDescriptorHeaps(_countof(renderTextureDescriptorHeaps), renderTextureDescriptorHeaps);

	//renderTextureObjectBundle->SetGraphicsRootDescriptorTable(0, renderTextureSRVDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

	//CD3DX12_GPU_DESCRIPTOR_HANDLE renderTextureCBVHandle(renderTextureSRVDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), 1, device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));

	//renderTextureObjectBundle->SetGraphicsRootDescriptorTable(1, renderTextureCBVHandle);

	//renderTextureObjectBundle->SetGraphicsRootDescriptorTable(2, samplerDescriptorHandle);

	//// 注意我们使用的渲染手法是三角形列表，也就是通常的Mesh网格
	//renderTextureObjectBundle->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	//renderTextureObjectBundle->IASetVertexBuffers(0, 1, &vertexBufferView);
	//renderTextureObjectBundle->IASetIndexBuffer(&indexBufferView);

	//renderTextureObjectBundle->DrawIndexedInstanced(indexCount, 1, 0, 0, 0);

	//renderTextureObjectBundle->Close();

	// 天空盒捆绑包
	skyboxBundle->SetGraphicsRootSignature(rootSignature.Get());

	// 设置要使用的描述符堆
	ID3D12DescriptorHeap* skyboxDescriptorHeaps[] = { skyboxDescriptorHeap.Get(), skyboxSamplerDescriptorHeap.Get() };
	skyboxBundle->SetDescriptorHeaps(_countof(skyboxDescriptorHeaps), skyboxDescriptorHeaps);

	CD3DX12_GPU_DESCRIPTOR_HANDLE skyboxConstantBufferViewHandle(GPU_DESCRIPTOR_HEAP_START(skyboxDescriptorHeap));

	skyboxBundle->SetGraphicsRootDescriptorTable(0, skyboxConstantBufferViewHandle);

	skyboxConstantBufferViewHandle.Offset(1, SRVCBVUAVDescriptorSize);

	skyboxBundle->SetGraphicsRootDescriptorTable(1, skyboxConstantBufferViewHandle);

	skyboxConstantBufferViewHandle.Offset(1, SRVCBVUAVDescriptorSize);

	skyboxBundle->SetGraphicsRootDescriptorTable(2, skyboxConstantBufferViewHandle);

	CD3DX12_GPU_DESCRIPTOR_HANDLE skyboxSamplerDescriptorHandle(GPU_DESCRIPTOR_HEAP_START(skyboxSamplerDescriptorHeap));

	skyboxBundle->SetGraphicsRootDescriptorTable(4, skyboxSamplerDescriptorHandle);

	// 注意我们使用的渲染手法是三角形列表，也就是通常的Mesh网格
	skyboxBundle->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	skyboxBundle->IASetVertexBuffers(0, 1, &skyboxVertexBufferView);
	skyboxBundle->IASetIndexBuffer(&skyboxIndexBufferView);

	skyboxBundle->DrawIndexedInstanced(skyboxIndexCount, 1, 0, 0, 0);

	skyboxBundle->Close();
}

void D3DApp::createMaterials()
{
	uint32_t materialConstantBufferIndex = 0;

	auto kanna = std::make_unique<Material>();
	kanna->name = "Kanna";
	kanna->materialConstantBufferIndex = materialConstantBufferIndex++;
	kanna->diffuseSRVHeapIndex = textures["Kanna"]->index;
	kanna->diffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
	kanna->fresnelR0 = { 0.05f, 0.05f, 0.05f };
	kanna->roughness = 0.2f;

	materials[kanna->name] = std::move(kanna);

	auto bunny = std::make_unique<Material>();
	bunny->name = "Bunny";
	bunny->materialConstantBufferIndex = materialConstantBufferIndex++;
	bunny->diffuseSRVHeapIndex = textures["Bunny"]->index;
	bunny->diffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
	bunny->fresnelR0 = { 0.05f, 0.05f, 0.05f };
	bunny->roughness = 0.2f;

	materials[bunny->name] = std::move(bunny);

	auto cartoonGrass = std::make_unique<Material>();
	cartoonGrass->name = "CartoonGrass";
	cartoonGrass->materialConstantBufferIndex = materialConstantBufferIndex++;
	cartoonGrass->diffuseSRVHeapIndex = textures["CartoonGrass"]->index;
	cartoonGrass->diffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
	cartoonGrass->fresnelR0 = { 0.05f, 0.05f, 0.05f };
	cartoonGrass->roughness = 0.2f;

	materials[cartoonGrass->name] = std::move(cartoonGrass);

	auto earth = std::make_unique<Material>();
	earth->name = "Earth";
	earth->materialConstantBufferIndex = materialConstantBufferIndex++;
	earth->diffuseSRVHeapIndex = textures["Earth"]->index;
	earth->diffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
	earth->fresnelR0 = { 0.05f, 0.05f, 0.05f };
	earth->roughness = 0.2f;

	materials[earth->name] = std::move(earth);

	auto ocean = std::make_unique<Material>();
	ocean->name = "Ocean";
	ocean->materialConstantBufferIndex = materialConstantBufferIndex++;
	ocean->diffuseSRVHeapIndex = textures["Ocean"]->index;
	ocean->diffuseAlbedo = { 1.0f, 1.0f, 1.0f, 0.5f };
	ocean->fresnelR0 = { 0.05f, 0.05f, 0.05f };
	ocean->roughness = 0.2f;

	materials[ocean->name] = std::move(ocean);

	auto wireFence = std::make_unique<Material>();
	wireFence->name = "WireFence";
	wireFence->materialConstantBufferIndex = materialConstantBufferIndex++;
	wireFence->diffuseSRVHeapIndex = textures["WireFence"]->index;
	wireFence->diffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
	wireFence->fresnelR0 = { 0.05f, 0.05f, 0.05f };
	wireFence->roughness = 0.2f;

	materials[wireFence->name] = std::move(wireFence);

	auto mirror = std::make_unique<Material>();
	mirror->name = "Mirror";
	mirror->materialConstantBufferIndex = materialConstantBufferIndex++;
	mirror->diffuseSRVHeapIndex = textures["Ice"]->index;
	mirror->diffuseAlbedo = { 1.0f, 1.0f, 1.0f, 0.3f };
	mirror->fresnelR0 = { 0.1f, 0.1f, 0.1f };
	mirror->roughness = 0.5f;

	materials[mirror->name] = std::move(mirror);

	auto marry = std::make_unique<Material>();
	marry->name = "Marry";
	marry->materialConstantBufferIndex = materialConstantBufferIndex++;
	marry->diffuseSRVHeapIndex = textures["Marry"]->index;
	marry->diffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
	marry->fresnelR0 = { 0.1f, 0.1f, 0.1f };
	marry->roughness = 0.5f;

	materials[marry->name] = std::move(marry);

	auto ground = std::make_unique<Material>();
	ground->name = "Ground";
	ground->materialConstantBufferIndex = materialConstantBufferIndex++;
	ground->diffuseSRVHeapIndex = textures["Ground"]->index;
	ground->diffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
	ground->fresnelR0 = { 0.1f, 0.1f, 0.1f };
	ground->roughness = 0.5f;

	materials[ground->name] = std::move(ground);

	auto shadow = std::make_unique<Material>();
	shadow->name = "Shadow";
	shadow->materialConstantBufferIndex = materialConstantBufferIndex++;
	shadow->diffuseSRVHeapIndex = textures["Default"]->index;
	shadow->diffuseAlbedo = { 0.0f, 0.0f, 0.0f, 0.5f };
	shadow->fresnelR0 = { 0.001f, 0.001f, 0.001f };
	shadow->roughness = 0.0f;

	materials[shadow->name] = std::move(shadow);

	auto treeSprites = std::make_unique<Material>();
	treeSprites->name = "TreeSprites";
	treeSprites->materialConstantBufferIndex = materialConstantBufferIndex++;
	treeSprites->diffuseSRVHeapIndex = textures["Tree"]->index;
	treeSprites->diffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
	treeSprites->fresnelR0 = { 0.01f, 0.01f, 0.01f };
	treeSprites->roughness = 0.125f;

	materials[treeSprites->name] = std::move(treeSprites);

	auto default = std::make_unique<Material>();
	default->name = "Default";
	default->materialConstantBufferIndex = materialConstantBufferIndex++;
	default->diffuseSRVHeapIndex = textures["Default"]->index;
	default->diffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
	default->fresnelR0 = { 0.1f, 0.1f, 0.1f };
	default->roughness = 0.5f;

	materials[default->name] = std::move(default);
}

void D3DApp::setMaterialSRVHeapHandles()
{
	for (auto& e : materials)
	{
		auto& material = e.second;

		material->textureHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(GPU_DESCRIPTOR_HEAP_START(shaderResourceViewDescriptorHeap),
																material->diffuseSRVHeapIndex, 
																SRVCBVUAVDescriptorSize);
	}
}

void D3DApp::createRenderItems()
{
	uint32_t objectConstantBufferIndex = 0;

	auto cubeRenderItem = std::make_unique<RenderItem>();
	cubeRenderItem->model = glm::translate(glm::mat4(1.0f), glm::vec3(-1.0f, 0.0f, 0.0f));
	cubeRenderItem->objectConstantBufferIndex = objectConstantBufferIndex++;
	cubeRenderItem->material = materials["Kanna"].get();
	cubeRenderItem->meshGeometry = meshGeometries["Cube"].get();
	auto submeshGeometry = cubeRenderItem->meshGeometry->drawArgs["Cube"];
	cubeRenderItem->indexCount = submeshGeometry.indexCount;
	cubeRenderItem->startIndexLocation = submeshGeometry.startIndexLocation;
	cubeRenderItem->baseVertexLocation = submeshGeometry.baseVertexLocation;

	renderItemLayer[(int)RenderLayer::Opaque].push_back(cubeRenderItem.get());
	allRenderItems.push_back(std::move(cubeRenderItem));

	auto bunnyRenderItem = std::make_unique<RenderItem>();
	bunnyRenderItem->model = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f));
	bunnyRenderItem->objectConstantBufferIndex = objectConstantBufferIndex++;
	bunnyRenderItem->material = materials["Bunny"].get();
	bunnyRenderItem->meshGeometry = meshGeometries["Bunny"].get();
	submeshGeometry = bunnyRenderItem->meshGeometry->drawArgs["Bunny"];
	bunnyRenderItem->indexCount = submeshGeometry.indexCount;
	bunnyRenderItem->startIndexLocation = submeshGeometry.startIndexLocation;
	bunnyRenderItem->baseVertexLocation = submeshGeometry.baseVertexLocation;

	renderItemLayer[(int)RenderLayer::Opaque].push_back(bunnyRenderItem.get());
	allRenderItems.push_back(std::move(bunnyRenderItem));

	auto land = std::make_unique<RenderItem>();
	land->model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -1.0f, 0.0f));
	land->objectConstantBufferIndex = objectConstantBufferIndex++;
	land->material = materials["CartoonGrass"].get();
	land->meshGeometry = meshGeometries["Land"].get();
	submeshGeometry = land->meshGeometry->drawArgs["Land"];
	land->indexCount = submeshGeometry.indexCount;
	land->startIndexLocation = submeshGeometry.startIndexLocation;
	land->baseVertexLocation = submeshGeometry.baseVertexLocation;

	renderItemLayer[(int)RenderLayer::Opaque].push_back(land.get());
	allRenderItems.push_back(std::move(land));

	auto sphere = std::make_unique<RenderItem>();
	sphere->model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 5.0f, -3.0f));
	sphere->objectConstantBufferIndex = objectConstantBufferIndex++;
	sphere->material = materials["Earth"].get();
	sphere->meshGeometry = meshGeometries["Sphere"].get();
	submeshGeometry = sphere->meshGeometry->drawArgs["Sphere"];
	sphere->indexCount = submeshGeometry.indexCount;
	sphere->startIndexLocation = submeshGeometry.startIndexLocation;
	sphere->baseVertexLocation = submeshGeometry.baseVertexLocation;
	sphereRenderItem = sphere.get();

	renderItemLayer[(int)RenderLayer::Opaque].push_back(sphere.get());

	auto reflectedSphere = std::make_unique<RenderItem>();

	*reflectedSphere = *sphere;
	reflectedSphere->objectConstantBufferIndex = objectConstantBufferIndex++;
	reflectedSphereRenderItem = reflectedSphere.get();

	renderItemLayer[(int)RenderLayer::Reflected].push_back(reflectedSphere.get());
	//renderItemLayer[(int)RenderLayer::Opaque].push_back(reflectedSphere.get());

	allRenderItems.push_back(std::move(sphere));
	allRenderItems.push_back(std::move(reflectedSphere));

	auto ocean = std::make_unique<RenderItem>();
	ocean->model = glm::mat4(1.0f);
	ocean->objectConstantBufferIndex = objectConstantBufferIndex++;
	ocean->material = materials["Ocean"].get();
	ocean->meshGeometry = meshGeometries["Ocean"].get();
	submeshGeometry = ocean->meshGeometry->drawArgs["Ocean"];
	ocean->indexCount = submeshGeometry.indexCount;
	ocean->startIndexLocation = submeshGeometry.startIndexLocation;
	ocean->baseVertexLocation = submeshGeometry.baseVertexLocation;

	wavesRenderItem = ocean.get();

	renderItemLayer[(int)RenderLayer::Transparent].push_back(ocean.get());

	allRenderItems.push_back(std::move(ocean));

	auto wireFence = std::make_unique<RenderItem>();
	wireFence->model = glm::translate(glm::mat4(1.0f), glm::vec3(-4.0f, 0.0f, 0.0f));
	wireFence->objectConstantBufferIndex = objectConstantBufferIndex++;
	wireFence->material = materials["WireFence"].get();
	wireFence->meshGeometry = meshGeometries["WireFenceCube"].get();
	submeshGeometry = wireFence->meshGeometry->drawArgs["WireFenceCube"];
	wireFence->indexCount = submeshGeometry.indexCount;
	wireFence->startIndexLocation = submeshGeometry.startIndexLocation;
	wireFence->baseVertexLocation = submeshGeometry.baseVertexLocation;

	renderItemLayer[(int)RenderLayer::AlphaTested].push_back(wireFence.get());

	allRenderItems.push_back(std::move(wireFence));

	auto mirror = std::make_unique<RenderItem>();
	mirror->model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 5.0f, 0.0f));
	mirror->objectConstantBufferIndex = objectConstantBufferIndex++;
	mirror->material = materials["Mirror"].get();
	mirror->meshGeometry = meshGeometries["Mirror"].get();
	submeshGeometry = mirror->meshGeometry->drawArgs["Mirror"];
	mirror->indexCount = submeshGeometry.indexCount;
	mirror->startIndexLocation = submeshGeometry.startIndexLocation;
	mirror->baseVertexLocation = submeshGeometry.baseVertexLocation;

	renderItemLayer[static_cast<int32_t>(RenderLayer::Mirrors)].push_back(mirror.get());
	renderItemLayer[static_cast<int32_t>(RenderLayer::Transparent)].push_back(mirror.get());

	allRenderItems.push_back(std::move(mirror));

	auto ground = std::make_unique<RenderItem>();
	auto model = glm::translate(glm::mat4(1.0f), glm::vec3(-4.0f, 1.45f, 0.0f));
	auto rotation = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
	auto scale = glm::scale(glm::mat4(1.0f), glm::vec3(0.5f, 0.5f, 0.5f));
	ground->model = model * rotation * scale;
	ground->objectConstantBufferIndex = objectConstantBufferIndex++;
	ground->material = materials["Ground"].get();
	ground->meshGeometry = meshGeometries["Mirror"].get();
	submeshGeometry = ground->meshGeometry->drawArgs["Mirror"];
	ground->indexCount = submeshGeometry.indexCount;
	ground->startIndexLocation = submeshGeometry.startIndexLocation;
	ground->baseVertexLocation = submeshGeometry.baseVertexLocation;

	renderItemLayer[static_cast<int32_t>(RenderLayer::Opaque)].push_back(ground.get());
	allRenderItems.push_back(std::move(ground));

	auto marryRenderItem = std::make_unique<RenderItem>();
	marryRenderItem->model = glm::translate(glm::mat4(1.0f), glm::vec3(-4.0f, 1.5f, 0.0f));
	marryRenderItem->objectConstantBufferIndex = objectConstantBufferIndex++;
	marryRenderItem->material = materials["Marry"].get();
	marryRenderItem->meshGeometry = meshGeometries["Marry"].get();
	submeshGeometry = marryRenderItem->meshGeometry->drawArgs["Marry"];
	marryRenderItem->indexCount = submeshGeometry.indexCount;
	marryRenderItem->startIndexLocation = submeshGeometry.startIndexLocation;
	marryRenderItem->baseVertexLocation = submeshGeometry.baseVertexLocation;

	renderItemLayer[static_cast<int32_t>(RenderLayer::Opaque)].push_back(marryRenderItem.get());

	// Shadowed skull will have different world matrix, so it needs to be its own render item.
	auto shadowedMarry = std::make_unique<RenderItem>();
	*shadowedMarry = *marryRenderItem;
	shadowedMarry->objectConstantBufferIndex = objectConstantBufferIndex++;
	shadowedMarry->material = materials["Shadow"].get();
	shadowedMarryRenderItem = shadowedMarry.get();
	renderItemLayer[static_cast<int32_t>(RenderLayer::Shadow)].push_back(shadowedMarry.get());

	allRenderItems.push_back(std::move(marryRenderItem));
	allRenderItems.push_back(std::move(shadowedMarry));

	auto treeSpritesRenderItem = std::make_unique<RenderItem>();
	treeSpritesRenderItem->model = glm::mat4(1.0f);
	treeSpritesRenderItem->objectConstantBufferIndex = objectConstantBufferIndex++;
	treeSpritesRenderItem->material = materials["TreeSprites"].get();
	treeSpritesRenderItem->meshGeometry = meshGeometries["TreeSpritesGeometry"].get();
	treeSpritesRenderItem->primitiveType = D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
	submeshGeometry = treeSpritesRenderItem->meshGeometry->drawArgs["Points"];
	treeSpritesRenderItem->indexCount = submeshGeometry.indexCount;
	treeSpritesRenderItem->startIndexLocation = submeshGeometry.startIndexLocation;
	treeSpritesRenderItem->baseVertexLocation = submeshGeometry.baseVertexLocation;

	renderItemLayer[static_cast<int32_t>(RenderLayer::AlphaTestedTreeSprites)].push_back(treeSpritesRenderItem.get());

	allRenderItems.push_back(std::move(treeSpritesRenderItem));
}

void D3DApp::createFrameResources()
{
	for (int i = 0; i < NumFrameResources; ++i)
	{
		frameResources.push_back(std::make_unique<FrameResource>(device.Get(), 2,
			static_cast<uint32_t>(allRenderItems.size()),
			static_cast<uint32_t>(materials.size()),
			waves->VertexCount()));
	}
}

void D3DApp::createFrameResourceConstantBufferViews()
{
	auto objectConstantBufferSize = ROUND_UP(sizeof(ObjectConstants), 256);

	// Need a CBV descriptor for each object for each frame resource
	for (auto frameIndex = 0; frameIndex < NumFrameResources; frameIndex++)
	{
		auto objectConstantBuffer = frameResources[frameIndex]->objectConstantBuffer->Resource();

		for (uint32_t i = 0; i < objectCount; i++)
		{
			D3D12_GPU_VIRTUAL_ADDRESS objectConstantBufferAddress = objectConstantBuffer->GetGPUVirtualAddress();

			// Offset to the ith object constant buffer in the buffer
			objectConstantBufferAddress += i * objectConstantBufferSize;

			// Offset to the object CBV in the descriptor heap
			auto objectHeapIndex = frameIndex * objectCount + i + textureCount;
			auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(CPU_DESCRIPTOR_HEAP_START(shaderResourceViewDescriptorHeap));
			handle.Offset(objectHeapIndex, SRVCBVUAVDescriptorSize);

			D3D12_CONSTANT_BUFFER_VIEW_DESC constantBufferViewDesc{};
			constantBufferViewDesc.BufferLocation = objectConstantBufferAddress;
			constantBufferViewDesc.SizeInBytes = objectConstantBufferSize;

			device->CreateConstantBufferView(&constantBufferViewDesc, handle);
		}
	}

	auto materialConstantBufferSize = ROUND_UP(sizeof(MaterialConstants), 256);

	// Need a CBV descriptor for each object for each frame resource
	for (auto frameIndex = 0; frameIndex < NumFrameResources; frameIndex++)
	{
		auto materialConstantBuffer = frameResources[frameIndex]->materialConstantBuffer->Resource();

		for (uint32_t i = 0; i < materialCount; i++)
		{
			D3D12_GPU_VIRTUAL_ADDRESS materialConstantBufferAddress = materialConstantBuffer->GetGPUVirtualAddress();

			// Offset to the ith object constant buffer in the buffer
			materialConstantBufferAddress += i * materialConstantBufferSize;

			// Offset to the object CBV in the descriptor heap
			auto materialHeapIndex = frameIndex * materialCount + i + textureCount + objectCount * NumFrameResources;
			auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(CPU_DESCRIPTOR_HEAP_START(shaderResourceViewDescriptorHeap));
			handle.Offset(materialHeapIndex, SRVCBVUAVDescriptorSize);

			D3D12_CONSTANT_BUFFER_VIEW_DESC constantBufferViewDesc{};
			constantBufferViewDesc.BufferLocation = materialConstantBufferAddress;
			constantBufferViewDesc.SizeInBytes = materialConstantBufferSize;

			device->CreateConstantBufferView(&constantBufferViewDesc, handle);
		}
	}

	auto passConstantBufferSize = ROUND_UP(sizeof(PassConstants), 256);

	for (uint32_t i = 0; i < 2; i++)
	{
		// Last three descriptors are the pass CBVs for each frame resource
		for (auto frameIndex = 0; frameIndex < NumFrameResources; frameIndex++)
		{
			auto passConstantBuffer = frameResources[frameIndex]->passConstantBuffer->Resource();

			D3D12_GPU_VIRTUAL_ADDRESS passConstantBufferAddress = passConstantBuffer->GetGPUVirtualAddress();

			passConstantBufferAddress += i * passConstantBufferSize;

			// Offset to the pass CBV in the descriptor heap
			auto passHeapIndex = passConstantBufferViewOffset + frameIndex + i * NumFrameResources;
			auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(shaderResourceViewDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
			handle.Offset(passHeapIndex, SRVCBVUAVDescriptorSize);

			D3D12_CONSTANT_BUFFER_VIEW_DESC constantBufferViewDesc{};
			constantBufferViewDesc.BufferLocation = passConstantBufferAddress;
			constantBufferViewDesc.SizeInBytes = passConstantBufferSize;

			device->CreateConstantBufferView(&constantBufferViewDesc, handle);

		}
	}
}

void D3DApp::createCommandAllocator(D3D12_COMMAND_LIST_TYPE type, ComPtr<ID3D12CommandAllocator>& commandAllocator, const std::wstring& name, uint32_t index)
{
	createCommandAllocator(type, commandAllocator.GetAddressOf(), name);
}

void D3DApp::createCommandAllocator(D3D12_COMMAND_LIST_TYPE type, ID3D12CommandAllocator** commandAllocator, const std::wstring& name, uint32_t index)
{
	std::wstring errorMesssage = L"";

	if (name.empty())
	{
		errorMesssage = std::wstring(L"Create ") + std::wstring(ObjectName(commandList)) + std::wstring(L"failed!");
	}
	else
	{
		errorMesssage = std::wstring(L"Create ") + name + std::wstring(L"failed!");
	}

	DXCheck(device->CreateCommandAllocator(type, IID_PPV_ARGS(commandAllocator)), errorMesssage.c_str());

	setD3D12DebugNameIndexd(*commandAllocator, name, index);
}

void D3DApp::createCommandList(D3D12_COMMAND_LIST_TYPE type, const ComPtr<ID3D12CommandAllocator>& commandAllocator,
														     const ComPtr<ID3D12PipelineState>& pipelineState, 
															 ComPtr<ID3D12GraphicsCommandList>& commandList, 
															 const std::wstring& name, uint32_t index)
{
	createCommandList(type, commandAllocator.Get(), pipelineState, commandList.GetAddressOf(), name);
}

void D3DApp::createCommandList(D3D12_COMMAND_LIST_TYPE type, ID3D12CommandAllocator* commandAllocator, 
															 const ComPtr<ID3D12PipelineState>& pipelineState, 
															 ID3D12GraphicsCommandList** commandList, 
															 const std::wstring& name, uint32_t index)
{
	std::wstring errorMesssage = L"";

	if (name.empty())
	{
		errorMesssage = std::wstring(L"Create ") + std::wstring(ObjectName(commandList)) + std::wstring(L"failed!");
	}
	else
	{
		errorMesssage = std::wstring(L"Create ") + name + std::wstring(L"failed!");
	}

	DXCheck(device->CreateCommandList(0,
		type,
		commandAllocator,
		pipelineState.Get(),
		IID_PPV_ARGS(commandList)), errorMesssage.c_str());

	setD3D12DebugNameIndexd(*commandList, name, index);
}

void D3DApp::createDepthStencilView(uint32_t width, uint32_t height, DXGI_FORMAT format, 
	const ComPtr<ID3D12DescriptorHeap>& depthStencilDescriptorHeap, 
	ComPtr<ID3D12Resource>& depthStencilBuffer)
{
	D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc{};
	depthStencilViewDesc.Format = depthStencilFormat;
	depthStencilViewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	depthStencilViewDesc.Flags = D3D12_DSV_FLAG_NONE;
	depthStencilViewDesc.Texture2D.MipSlice = 0;

	D3D12_CLEAR_VALUE depthStencilOptimizedClearValue{};
	depthStencilOptimizedClearValue.Format = format;
	depthStencilOptimizedClearValue.DepthStencil.Depth = 1.0f;
	depthStencilOptimizedClearValue.DepthStencil.Stencil = 0;

	DXCheck(device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Tex2D(format,
			width, height,
			1,
			0,
			1,
			0,
			D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL),
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&depthStencilOptimizedClearValue,
		IID_PPV_ARGS(depthStencilBuffer.GetAddressOf())),
		L"Create depth stencil buffer failed!");

	depthStencilBuffer->SetName(L"depthStencilBuffer");

	CD3DX12_CPU_DESCRIPTOR_HANDLE depthStencilBufferViewHandle(CPU_DESCRIPTOR_HEAP_START(depthStencilDescriptorHeap));

	device->CreateDepthStencilView(depthStencilBuffer.Get(), &depthStencilViewDesc, depthStencilBufferViewHandle);
}

void D3DApp::initializeDirect3D()
{
	createDebugLayer();
	createDXGIFactory();
	createDevice();
	createCommandQueue();
	createSwapChain();
	//createRenderTargetView();
	createRootSignature();
	Utils::createDXCCompiler();
	createShadersAndInputlayouts();
	createGraphicsPipelineState();
	createSkyboxGraphicsPipelineState();
	createCommandLists();
	createFence();
	
	// 创建NM大小的上传堆，纹理，顶点缓冲和索引缓冲都会用到
	createUploadHeap(ROUND_UP(1024 * 1024 * 100, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT));
	loadResources();
	createMaterials();
	createRenderItems();
	createShaderResourceViewDescriptorHeap();
	createTextureShaderResourceViews();
	setMaterialSRVHeapHandles();
	createRenderTextureRTVDescriptorHeap();
	createRenderTextureRTV(windowWidth, windowHeight);
	createRenderTextureSRVDescriptorHeap();
	createRenderTextureSRV();
	createRenderTextureDSVDescriptorHeap();
	createRenderTextureDSV(windowWidth, windowHeight);
	createSkyboxDescriptorHeap();
	createSkyboxDescriptors();
	createVertexBuffer(bunny);
	createIndexBuffer(bunny);
	createSkyboxVertexBuffer(skybox);
	createSkyboxIndexBuffer(skybox);
	createConstantBufferView();
	createSkyboxConstantBufferView();
	createRenderTextureConstantBufferView();
	createDepthStencilDescriptorHeap();
	createSamplerDescriptorHeap();
	createSamplers();
	createSkyboxSamplerDescriptorHeap();
	createSkyboxSampler();
	initImGui();
	createFrameResources();
	createFrameResourceConstantBufferViews();
	recordCommands();
	onResize();
}

void D3DApp::initImGui()
{
	ImGuiInitData imGuiInitData{ window, mainWindow, device, FrameBackbufferCount, imGuiShaderResourceViewDescriptorHeap };
	imGuiLayer.initialize(imGuiInitData);
}

void D3DApp::updateImGui()
{
	imGuiLayer.update();
}

void D3DApp::onGUIRender()
{
	imGuiLayer.dockSpaceBegin();

	// 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
	if (showDemoWindow)
		ImGui::ShowDemoWindow(&showDemoWindow);

	// 2. Show a simple window that we create ourselves. We use a Begin/End pair to create a named window.
	{
		static float f = 0.0f;
		static int counter = 0;

		bool isOpen = true;
		ImGui::Begin("Hello, world!", &isOpen, ImGuiWindowFlags_AlwaysAutoResize);                          // Create a window called "Hello, world!" and append into it.

		ImGui::Text("This is some useful text.");               // Display some text (you can use a format strings too)
		ImGui::Checkbox("Demo Window", &showDemoWindow);      // Edit bools storing our window open/close state
		ImGui::Checkbox("Another Window", &showAnotherWindow);

		ImGui::ColorEdit3("Clear color", (float*)&clearColor); // Edit 3 floats representing a color

		ImGui::SliderFloat("Sun Theta:", &sunTheta, 0.0f, glm::pi<float>() * 2.0f);
		ImGui::SliderFloat("Sun Phi:", &sunPhi, 0.0f, glm::pi<float>() * 2.0f);

		ImGui::SliderFloat3("Directional Light0:", (float*)&mainPassConstants.lights[1].direction, -1.0f, 1.0f);
		ImGui::SliderFloat3("Directional Light1:", (float*)&mainPassConstants.lights[2].direction, -1.0f, 1.0f);

		ImGui::SliderFloat("Camera Speed:", &camera.MovementSpeed, 5.0f, 10.0f);

		ImGui::Text("Camera Distance:%f", glm::length(camera.Position));

		if (ImGui::Button("Button"))                            // Buttons return true when clicked (most widgets return true when edited/activated)
			counter++;
		ImGui::SameLine();
		ImGui::Text("counter = %d", counter);
		ImGui::Text("Vertex Count = %d", vertexCount);
		ImGui::Text("Triangle Count = %d", triangleCount);
		ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
		ImGui::End();
	}

	// 这里设置的是窗口为非Docking状态下的尺寸
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f)); // Set window background to red
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin("Viewport");
	ImGui::PopStyleColor();
	auto texture = CD3DX12_GPU_DESCRIPTOR_HANDLE(imGuiShaderResourceViewDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), 8, SRVCBVUAVDescriptorSize);

	viewportWidth = static_cast<uint32_t>(ImGui::GetContentRegionAvail().x);
	viewportHeight = static_cast<uint32_t>(ImGui::GetContentRegionAvail().y);

	auto currentAspectRatio = static_cast<float>(viewportWidth) /
								   static_cast<float>(viewportHeight);

	// 修改渲染到纹理时的视口和裁剪矩形大小
	imGuiViewport = { 0.0f, 0.0f, static_cast<float>(viewportWidth), static_cast<float>(viewportHeight) };
	imGuiScissorRect = { 0, 0, viewportWidth, viewportHeight };

	if (currentAspectRatio != aspectRatio)
	{
		aspectRatio = currentAspectRatio;

		onRenderTextureResize(viewportWidth, viewportHeight);
	}

	//ImDrawList* draw_list = ImGui::GetWindowDrawList();
	//draw_list->AddCallback([](const ImDrawList* parent_list, const ImDrawCmd* cmd)
	//	{
	//		auto callbackData = static_cast<D3DApp*>(cmd->UserCallbackData);
	//		callbackData->disableAlphaBlend();

	//	}, this);
	ImGui::Image((ImTextureID)texture.ptr, { static_cast<float>(viewportWidth), static_cast<float>(viewportHeight) });

	ImGui::End();
	ImGui::PopStyleVar();

	// 3. Show another simple window.
	if (showAnotherWindow)
	{
		ImGui::Begin("Another Window", &showAnotherWindow, ImGuiWindowFlags_AlwaysAutoResize);   // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
		ImGui::Text("Hello from another window!");
		if (ImGui::Button("Close Me"))
			showAnotherWindow = false;
		ImGui::End();
	}
	
	imGuiLayer.docSpaceEnd();
}

void D3DApp::renderImGui(const ComPtr<ID3D12GraphicsCommandList>& commandList)
{
	imGuiLayer.render(commandList);
}

void D3DApp::shutdownImGui()
{
	// Cleanup
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

void D3DApp::updateConstantBuffers()
{
	// 准备一个简单的旋转MVP矩阵 让方块转起来
	auto currentTimeDuration = std::chrono::high_resolution_clock::now().time_since_epoch();

	auto currentTime = std::chrono::duration<float, std::chrono::seconds::period>(currentTimeDuration).count();

	//计算旋转的角度：旋转角度(弧度) = 时间(秒) * 角速度(弧度/秒)
	//下面这句代码相当于经典游戏消息循环中的OnUpdate函数中需要做的事情
	modelRotationYAngle += static_cast<float>((currentTime - startTime) * angularVelocity);

	startTime = currentTime;

	// 旋转角度是2PI周期的倍数，去掉周期数，只留下相对0弧度开始的小于2PI的弧度即可
	if (modelRotationYAngle > XM_2PI)
	{
		modelRotationYAngle = std::fmodf(modelRotationYAngle, XM_2PI);
	}

	auto model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f));
	auto view = camera.GetViewMatrix();
	projection = glm::perspective(glm::radians(45.0f), aspectRatio, 1.0f, 1000.0f);

	auto modelViewProjection = projection * view * model;

	objectConstants->model = model;

	renderTextureConstants->model = model;
	renderTextureConstants->modelViewProjection = modelViewProjection;

	auto currentConstantBuffer = currentFrameResource->objectConstantBuffer.get();

	// Update shadow world matrix.
	XMVECTOR shadowPlane = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f); // xz plane
	XMFLOAT3 mainLightDirection = { 0.0f, -1.0f, 0.0f };
	XMVECTOR toMainLight = -XMLoadFloat3(&mainLightDirection);
	XMMATRIX S = XMMatrixShadow(shadowPlane, toMainLight);
	XMMATRIX shadowOffsetY = XMMatrixTranslation(0.0f, 1.5f, 0.0f);

	XMMATRIX marryWorld = XMMatrixTranslation(-4.0f, 1.6f, 0.0f);
	XMFLOAT4X4 marryWorld4X4;

	XMStoreFloat4x4(&marryWorld4X4, marryWorld * S * shadowOffsetY);

	memcpy_s(&shadowedMarryRenderItem->model[0][0], sizeof(float) * 16, marryWorld4X4.m, sizeof(float) * 16);

	shadowedMarryRenderItem->numFramesDirty = NumFrameResources;

	for (const auto& renderItem : allRenderItems)
	{
		// Only update the cbuffer data if the constants have changed
		// This needs to be tracked per frame resource
		if (renderItem->numFramesDirty > 0)
		{
			ObjectConstants objectConstants;
			objectConstants.model = renderItem->model;
			objectConstants.textureTransform = glm::mat4(1.0f);

			currentConstantBuffer->CopyData(renderItem->objectConstantBufferIndex, objectConstants);

			// Next FrameResource need to be updated too
			renderItem->numFramesDirty--;
		}
	}
}

void D3DApp::updateMaterialConstantBuffers()
{
	auto currMaterialConstantBuffer = currentFrameResource->materialConstantBuffer.get();
	for (auto& e : materials)
	{
		// Only update the cbuffer data if the constants have changed.  If the cbuffer
		// data changes, it needs to be updated for each FrameResource.
		Material* material = e.second.get();
		if (material->numFramesDirty > 0)
		{
			MaterialConstants materialConstants;
			materialConstants.diffuseAlbedo = material->diffuseAlbedo;
			materialConstants.fresnelR0 = material->fresnelR0;
			materialConstants.roughness = material->roughness;
			materialConstants.materialTransform = material->materialTransform;

			currMaterialConstantBuffer->CopyData(material->materialConstantBufferIndex, materialConstants);

			// Next FrameResource need to be updated too.
			material->numFramesDirty--;
		}
	}
}

void D3DApp::updateSkyboxConstantBuffer()
{
	static float angle = 0.0f;

	auto model = glm::rotate(glm::mat4(1.0f), glm::radians(angle += FrameTime), glm::vec3(0.0f, 1.0f, 0.0f));;
	//auto view = camera.GetViewMatrix();
	auto view = glm::mat4(glm::mat3(camera.GetViewMatrix()));
	projection = glm::perspective(glm::radians(45.0f), aspectRatio, 0.1f, 100.0f);

	auto modelViewProjection = projection * view * model;

	skyboxConstants->model = model;
	skyboxConstants->modelViewProjection = modelViewProjection;

	skyboxPassConstants.fogColor = { 0.7f, 0.7f, 0.7f, 1.0f };
	skyboxPassConstants.fogStart = 5.0f;
	skyboxPassConstants.fogRange = 150.0f;
}

void D3DApp::updateMainPassConstantBuffer()
{
	auto view = camera.GetViewMatrix();
	projection = glm::perspective(glm::radians(camera.Zoom), aspectRatio, 1.0f, 1000.0f);

	auto viewProjection = projection * view;

	mainPassConstants.viewProjection = viewProjection;
	mainPassConstants.eyePosition = camera.Position;
	mainPassConstants.nearZ = 1.0f;
	mainPassConstants.farZ = 1000.0f;
	mainPassConstants.totalTime = gameTimer.TotalTime();
	mainPassConstants.deltaTime = gameTimer.DeltaTime();

	mainPassConstants.ambientLight = { 0.25f, 0.25f, 0.35f, 1.0f };

	glm::vec4 lightDirection = MathHelper::SphericalToCartesian(10.0f, sunTheta, sunPhi);

	mainPassConstants.lights[0].direction = lightDirection;
	mainPassConstants.lights[0].strength = { 0.6f, 0.6f, 0.6f };
	//passConstants.lights[0].strength = { std::sinf(passConstants.totalTime) * 0.5f + 0.5f, 0.0f, 0.0f };
	//passConstants.lights[0].direction = { 0.57735f, -0.57735f, 0.57735f };

	auto currentPassConstantBuffer = currentFrameResource->passConstantBuffer.get();

	currentPassConstantBuffer->CopyData(0, mainPassConstants);
}

void D3DApp::updateReflectedPassConstantBuffer()
{
	reflectedPassConstants = mainPassConstants;

	XMMATRIX world = XMMatrixTranslation(0.0f, 5.0f, -3.0f);

	// Update reflection world matrix.
	XMVECTOR mirrorPlane = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f); // xy plane
	XMMATRIX R = XMMatrixReflect(mirrorPlane);
	XMFLOAT4X4 reflectedWorld;

	glm::mat4 reflection;
	XMFLOAT4X4 reflection4X4;

	DirectX::XMStoreFloat4x4(&reflection4X4, R);

	memcpy_s(&reflection[0][0], sizeof(float) * 16, reflection4X4.m, sizeof(float) * 16);

	DirectX::XMStoreFloat4x4(&reflectedWorld, world * R);

	memcpy_s(&reflectedSphereRenderItem->model[0][0], sizeof(float) * 16, reflectedWorld.m, sizeof(float) * 16);

	reflectedSphereRenderItem->numFramesDirty = NumFrameResources;

	// Reflect the lighting
	for (auto i = 0; i < 4; i++)
	{
		glm::vec4 lightDirection = glm::vec4(mainPassConstants.lights[i].direction, 0.0f);
		lightDirection = reflection * lightDirection;
		reflectedPassConstants.lights[i].direction = lightDirection;
	}

	// Reflected pass stored in index 1
	auto currentPasssConstantBuffer = currentFrameResource->passConstantBuffer.get();
	currentPasssConstantBuffer->CopyData(1, reflectedPassConstants);
}

void D3DApp::calculateFrameStats()
{
	// Code computes the average frames per second, and also the 
	// average time it takes to render one frame.  These stats 
	// are appended to the window caption bar.

	static int frameCount = 0;
	static float timeElapsed = 0.0f;

	frameCount++;

	// Compute averages over one second period.
	if ((gameTimer.TotalTime() - timeElapsed) >= 1.0f)
	{
		float fps = static_cast<float>(frameCount); // fps = frameCnt / 1
		float mspf = 1000.0f / fps;

		std::wstring fpsStr = std::to_wstring(fps);
		std::wstring mspfStr = std::to_wstring(mspf);

		std::wstring windowText = windowTitle +
			L"    fps: " + fpsStr +
			L"   mspf: " + mspfStr;

		SetWindowText(mainWindow, windowText.c_str());

		// Reset for next average.
		frameCount = 0;
		timeElapsed += 1.0f;
	}
}

void D3DApp::updateOcean()
{
	// Every quarter second, generate a random wave.
	static float base = 0.0f;
	if ((gameTimer.TotalTime() - base) >= 0.25f)
	{
		base += 0.25f;

		int i = MathHelper::Random(4, waves->RowCount() - 5);
		int j = MathHelper::Random(4, waves->ColumnCount() - 5);

		float r = MathHelper::RandomF(0.2f, 0.5f);

		waves->Disturb(i, j, r);
	}

	// Update the wave simulation.
	waves->Update(gameTimer.DeltaTime());

	// Update the wave vertex buffer with the new solution.
	auto currWavesVertexBuffer = currentFrameResource->wavesVertexBuffer.get();
	for (int i = 0; i < waves->VertexCount(); ++i)
	{
		DXVertex vertex;

		vertex.position = waves->Position(i);
		vertex.normal = waves->Normal(i);

		// Derive texcoords from position by 
		// mapping [-w / 2, w / 2] --> [0,1]
		vertex.texcoord.x = 0.5f + vertex.position.x / waves->Width();
		vertex.texcoord.y = 0.5f - vertex.position.z / waves->Depth();
		vertex.texcoord *= 10.0f;
		vertex.color = { 0.0f, 0.0f, 1.0f, 1.0f };

		currWavesVertexBuffer->CopyData(i, vertex);
	}

	// Set the dynamic VB of the wave renderitem to the current frame VB.
	wavesRenderItem->meshGeometry->vertexBufferGPU = currWavesVertexBuffer->Resource();
}

void D3DApp::animateOceanTexture()
{
	// Scroll the water material texture coordinates.
	auto oceanMaterial = materials["Ocean"].get();

	float& u = oceanMaterial->materialTransform[3][0];
	float& v = oceanMaterial->materialTransform[3][1];

	u += 0.1f * gameTimer.DeltaTime();
	v += 0.02f * gameTimer.DeltaTime();

	if (u >= 1.0f)
		u -= 1.0f;

	if (v >= 1.0f)
		v -= 1.0f;

	oceanMaterial->materialTransform[3][0] = u;
	oceanMaterial->materialTransform[3][1] = v;

	// Material has changed, so need to update cbuffer.
	oceanMaterial->numFramesDirty = NumFrameResources;
}

void D3DApp::update()
{
	updateFrameResourceAndIndex();

	waitFrameResource();

	updateConstantBuffers();
	updateMaterialConstantBuffers();
	updateSkyboxConstantBuffer();
	updateMainPassConstantBuffer();
	updateReflectedPassConstantBuffer();
	updateOcean();
	animateOceanTexture();
}

void D3DApp::waitFrameResource()
{
	// Has the GPU finished processing the commands of the current frame resource?
	// If not, wait until the GPU has completed commands up to this fence point
	auto completeValue = fence->GetCompletedValue();
	if (currentFrameResource->fence != 0 && fence->GetCompletedValue() < currentFrameResource->fence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
		DXCheck(fence->SetEventOnCompletion(currentFrameResource->fence, eventHandle), L"ID3D12Fence::SetEventOnCompletion failed!");

		if (eventHandle != nullptr)
		{
			WaitForSingleObject(eventHandle, INFINITE);
			CloseHandle(eventHandle);
		}
	}
}

void D3DApp::updateFrameResourceAndIndex()
{
	// Cycle through the circular frame resource array
	currentFrameResourceIndex = (currentFrameResourceIndex + 1) % NumFrameResources;
	currentFrameResource = frameResources[currentFrameResourceIndex].get();
}

void D3DApp::processInput(float deltaTime)
{
	if (glfwGetKey(window, GLFW_KEY_W))
	{
		camera.ProcessKeyboard(FORWARD, deltaTime);
	}

	if (glfwGetKey(window, GLFW_KEY_S))
	{
		camera.ProcessKeyboard(BACKWARD, deltaTime);
	}

	if (glfwGetKey(window, GLFW_KEY_A))
	{
		camera.ProcessKeyboard(LEFT, deltaTime);
	}

	if (glfwGetKey(window, GLFW_KEY_D))
	{
		camera.ProcessKeyboard(RIGHT, deltaTime);
	}

	if (glfwGetKey(window, GLFW_KEY_Q))
	{
		camera.ProcessKeyboard(UP, deltaTime);
	}

	if (glfwGetKey(window, GLFW_KEY_E))
	{
		camera.ProcessKeyboard(DOWN, deltaTime);
	}

	if (glfwGetKey(window, GLFW_KEY_UP))
	{
		sunPhi += deltaTime;
	}

	if (glfwGetKey(window, GLFW_KEY_DOWN))
	{
		sunPhi -= deltaTime;
	}

	if (glfwGetKey(window, GLFW_KEY_LEFT))
	{
		sunTheta -= deltaTime;
	}

	if (glfwGetKey(window, GLFW_KEY_RIGHT))
	{
		sunTheta += deltaTime;
	}
}

void D3DApp::render()
{
	resetFrameResourceCommandAllocator();

	// 第一遍，渲染到纹理
	{
		//设置屏障将渲染目标纹理从资源转换为渲染目标状态
		resourceTransition(renderTexture,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
			 D3D12_RESOURCE_STATE_RENDER_TARGET);

		CD3DX12_CPU_DESCRIPTOR_HANDLE renderTextureRTVDescriptorHandle(CPU_DESCRIPTOR_HEAP_START(renderTextureRTVDescriptorHeap));
		CD3DX12_CPU_DESCRIPTOR_HANDLE renderTextureDSVDecriptorHandle(CPU_DESCRIPTOR_HEAP_START(renderTextureDSVDescriptorHeap));

		commandListDirectPre->OMSetRenderTargets(1, &renderTextureRTVDescriptorHandle, FALSE, &renderTextureDSVDecriptorHandle);

		// 渲染到纹理时使用ImGui中视口的大小
		commandListDirectPre->RSSetViewports(1, &imGuiViewport);
		commandListDirectPre->RSSetScissorRects(1, &imGuiScissorRect);

		commandListDirectPre->ClearRenderTargetView(renderTextureRTVDescriptorHandle, (float*)&clearColor, 0, nullptr);
		commandListDirectPre->ClearDepthStencilView(renderTextureDSVDecriptorHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

		renderScene();

	////	// 这里也要设置一次描述符堆，和bundle录制时的设置要严格匹配
	////	ID3D12DescriptorHeap* skyboxDescriptorHeaps[] = { skyboxDescriptorHeap.Get(), skyboxSamplerDescriptorHeap.Get() };
	////	commandListDirectPre->SetDescriptorHeaps(_countof(skyboxDescriptorHeaps), skyboxDescriptorHeaps);
	////	commandListDirectPre->ExecuteBundle(skyboxBundle.Get());

	////	// 这里也要设置一次描述符堆，和bundle录制时的设置要严格匹配
	////	ID3D12DescriptorHeap* descriptorHeaps[] = { shaderResourceViewDescriptorHeap.Get(), samplerDescriptorHeap.Get() };
	////	commandListDirectPre->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
	////	commandListDirectPre->ExecuteBundle(sceneBundle.Get());

		//渲染完毕将渲染目标纹理由渲染目标状态转换回纹理状态，并准备显示
		resourceTransition(renderTexture,
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

		executeCommandLists();

		advanceFrameResourceFence();

		waitFrameResource();
	}

	resetFrameResourceCommandAllocator();

	//开始第二遍渲染，渲染到屏幕，并显示纹理渲染的结果
	commandListDirectPre->RSSetViewports(1, &viewport);
	commandListDirectPre->RSSetScissorRects(1, &scissorRect);

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
	beginRenderTargetTransition(renderTargets[currentFrameIndex]);

	CD3DX12_CPU_DESCRIPTOR_HANDLE renderTargetViewDescriptorHandle(CPU_DESCRIPTOR_HEAP_START(renderTargetViewDescriptorHeap),
		currentFrameIndex, renderTargetViewDescriptorSize);

	CD3DX12_CPU_DESCRIPTOR_HANDLE depthStencilBufferViewHandle(CPU_DESCRIPTOR_HEAP_START(depthStencilViewDescriptorHeap));

	commandListDirectPre->OMSetRenderTargets(1, &renderTargetViewDescriptorHandle, FALSE, &depthStencilBufferViewHandle);

	commandListDirectPre->ClearRenderTargetView(renderTargetViewDescriptorHandle, (float*)&clearColor, 0, nullptr);
	commandListDirectPre->ClearDepthStencilView(depthStencilBufferViewHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	commandListDirectPre->SetPipelineState(pipelineStates["Opaque"].Get());

	//renderScene();

	ID3D12DescriptorHeap* imGuiDescriptorHeaps[] = { imGuiShaderResourceViewDescriptorHeap.Get() };
	commandListDirectPre->SetDescriptorHeaps(1, imGuiDescriptorHeaps);

	renderImGui(commandListDirectPre);

	endRenderTargetTransition(renderTargets[currentFrameIndex]);

	executeCommandLists();

	present();

	advanceFrameResourceFence();
}

void D3DApp::renderScene()
{
	// 天空盒
	commandListDirectPre->SetGraphicsRootSignature(rootSignature.Get());
	commandListDirectPre->SetPipelineState(skyboxGraphicsPipelineState.Get());

	// 设置要使用的描述符堆
	ID3D12DescriptorHeap* skyboxDescriptorHeaps[] = { skyboxDescriptorHeap.Get(), skyboxSamplerDescriptorHeap.Get() };
	commandListDirectPre->SetDescriptorHeaps(_countof(skyboxDescriptorHeaps), skyboxDescriptorHeaps);

	CD3DX12_GPU_DESCRIPTOR_HANDLE skyboxConstantBufferViewHandle(GPU_DESCRIPTOR_HEAP_START(skyboxDescriptorHeap));

	commandListDirectPre->SetGraphicsRootDescriptorTable(0, skyboxConstantBufferViewHandle);

	skyboxConstantBufferViewHandle.Offset(1, SRVCBVUAVDescriptorSize);

	commandListDirectPre->SetGraphicsRootDescriptorTable(1, skyboxConstantBufferViewHandle);

	skyboxConstantBufferViewHandle.Offset(1, SRVCBVUAVDescriptorSize);

	commandListDirectPre->SetGraphicsRootDescriptorTable(2, skyboxConstantBufferViewHandle);

	CD3DX12_GPU_DESCRIPTOR_HANDLE skyboxSamplerDescriptorHandle(GPU_DESCRIPTOR_HEAP_START(skyboxSamplerDescriptorHeap));

	commandListDirectPre->SetGraphicsRootDescriptorTable(4, skyboxSamplerDescriptorHandle);

	// 注意我们使用的渲染手法是三角形列表，也就是通常的Mesh网格
	commandListDirectPre->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandListDirectPre->IASetVertexBuffers(0, 1, &skyboxVertexBufferView);
	commandListDirectPre->IASetIndexBuffer(&skyboxIndexBufferView);

	commandListDirectPre->DrawIndexedInstanced(skyboxIndexCount, 1, 0, 0, 0);

	if (wireframe)
	{
		commandListDirectPre->SetPipelineState(pipelineStates["Wireframe"].Get());
	}
	else
	{
		commandListDirectPre->SetPipelineState(pipelineStates["Opaque"].Get());
	}

	// 设置要使用的描述符堆
	ID3D12DescriptorHeap* descriptorHeaps[] = { shaderResourceViewDescriptorHeap.Get(), samplerDescriptorHeap.Get() };
	commandListDirectPre->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	CD3DX12_GPU_DESCRIPTOR_HANDLE samplerDescriptorHandle(GPU_DESCRIPTOR_HEAP_START(samplerDescriptorHeap), currentSamplerNo, samplerDescriptorSize);

	// Sampler
	commandListDirectPre->SetGraphicsRootDescriptorTable(4, samplerDescriptorHandle);

	// PassContants
	auto passConstantBufferViewIndex = passConstantBufferViewOffset + currentFrameResourceIndex;
	auto passConstantBufferViewHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(GPU_DESCRIPTOR_HEAP_START(shaderResourceViewDescriptorHeap));
	passConstantBufferViewHandle.Offset(passConstantBufferViewIndex, SRVCBVUAVDescriptorSize);

	commandListDirectPre->SetGraphicsRootDescriptorTable(3, passConstantBufferViewHandle);

	// 绘制不透明物体
	drawRenderItems(commandListDirectPre, renderItemLayer[static_cast<int32_t>(RenderLayer::Opaque)]);

	// 将镜子可见部分像素在模板缓冲中用数值1标记(这一步不会写入渲染目标，也就是镜子不可见)
	commandListDirectPre->OMSetStencilRef(1);
	commandListDirectPre->SetPipelineState(pipelineStates["MarkStencilMirrors"].Get());

	// 绘制镜子
	drawRenderItems(commandListDirectPre, renderItemLayer[static_cast<int32_t>(RenderLayer::Mirrors)]);

	commandListDirectPre->SetPipelineState(pipelineStates["DrawStencilReflections"].Get());

	// 只将反射画到镜子里（只针对网板缓冲区为1的像素）
	// 请注意，我们必须提供一个不同的pass常量缓冲——它包含了反射的光照(方向)
	auto reflectedPassConstantBufferViewHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(
		GPU_DESCRIPTOR_HEAP_START(shaderResourceViewDescriptorHeap),
		passConstantBufferViewIndex + 3,
		SRVCBVUAVDescriptorSize);

	commandListDirectPre->SetGraphicsRootDescriptorTable(3, reflectedPassConstantBufferViewHandle);

	drawRenderItems(commandListDirectPre, renderItemLayer[static_cast<int32_t>(RenderLayer::Reflected)]);

	// 恢复main pass常量缓冲和模板参考值
	commandListDirectPre->SetGraphicsRootDescriptorTable(3, passConstantBufferViewHandle);
	commandListDirectPre->OMSetStencilRef(0);

	commandListDirectPre->SetPipelineState(pipelineStates["AlphaTested"].Get());
	drawRenderItems(commandListDirectPre.Get(), renderItemLayer[static_cast<int32_t>(RenderLayer::AlphaTested)]);

	commandListDirectPre->SetPipelineState(pipelineStates["TreeSprites"].Get());
	drawRenderItems(commandListDirectPre.Get(), renderItemLayer[static_cast<int32_t>(RenderLayer::AlphaTestedTreeSprites)]);

	// Draw shadows
	commandListDirectPre->SetPipelineState(pipelineStates["Shadow"].Get());
	drawRenderItems(commandListDirectPre.Get(), renderItemLayer[static_cast<int32_t>(RenderLayer::Shadow)]);

	// 绘制具有透明度的镜子，以使反射融合在一起
	commandListDirectPre->SetPipelineState(pipelineStates["Transparent"].Get());
	drawRenderItems(commandListDirectPre.Get(), renderItemLayer[static_cast<int32_t>(RenderLayer::Transparent)]);
}

void D3DApp::present()
{
	DXCheck(swapChain->Present(1, 0), L"IDXGISwapChain::Present failed!");

	// 到这里说明一个命令队列完整的执行完了，在这里就代表我们的一帧已经渲染完了，接着准备执行下一帧渲染
	// 获取新的后缓冲序号，因为Present真正完成时后缓冲的序号就更新了
	//currentFrameIndex = swapChain->GetCurrentBackBufferIndex();
	currentFrameIndex = (currentFrameIndex + 1) % FrameBackbufferCount;
}

void D3DApp::executeCommandLists()
{
	DXCheck(commandListDirectPre->Close(), L"ID3D12GraphicsCommandList::Close failed!");

	commandlists.clear();
	commandlists.emplace_back(commandListDirectPre.Get());

	commandQueue->ExecuteCommandLists(static_cast<uint32_t>(commandlists.size()), commandlists.data());
}

void D3DApp::advanceFrameResourceFence()
{
	// Advance the fence value to mark commands up to this fence point.
	currentFrameResource->fence = ++fenceValue;

	// Add an instruction to the command queue to set a new fence point. 
	// Because we are on the GPU timeline, the new fence point won't be 
	// set until the GPU finishes processing all the commands prior to this Signal().
	commandQueue->Signal(fence.Get(), fenceValue);
}

void D3DApp::beginRenderTargetTransition(const ComPtr<ID3D12Resource>& renderTarget)
{
	commandListDirectPre->ResourceBarrier(1,
		&CD3DX12_RESOURCE_BARRIER::Transition(renderTarget.Get(),
			D3D12_RESOURCE_STATE_PRESENT,
			D3D12_RESOURCE_STATE_RENDER_TARGET));
}

void D3DApp::endRenderTargetTransition(const ComPtr<ID3D12Resource>& renderTarget)
{
	commandListDirectPre->ResourceBarrier(1, 
		&CD3DX12_RESOURCE_BARRIER::Transition(renderTarget.Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, 
		 D3D12_RESOURCE_STATE_PRESENT));
}

void D3DApp::resourceTransition(const ComPtr<ID3D12Resource>& resource, D3D12_RESOURCE_STATES stateBefore, D3D12_RESOURCE_STATES stateAfter)
{
	commandListDirectPre->ResourceBarrier(1,
		&CD3DX12_RESOURCE_BARRIER::Transition(resource.Get(),
			stateBefore,
			stateAfter));
}

void D3DApp::resetFrameResourceCommandAllocator()
{
	if (currentFrameResource != nullptr)
	{
		commandAllocatorDirectPre = currentFrameResource->commandListAllocator;
	}

	// 命令分配器先Reset一下
	DXCheck(commandAllocatorDirectPre->Reset(), L"ID3D12CommandAllocator::Reset failed!");

	// Reset命令列表，并重新指定命令分配器和PSO对象
	DXCheck(commandListDirectPre->Reset(commandAllocatorDirectPre.Get(), pipelineStates["Opaque"].Get()), L"ID3D12GraphicsCommandList::Reset failed!");
}

void D3DApp::drawRenderItems(const ComPtr<ID3D12GraphicsCommandList>& commandList, const std::vector<RenderItem*>& renderItmes)
{
	auto objectConstantBufferByteSize = Utils::calculateConstantBufferByteSize(sizeof(ObjectConstants));
	auto materialConstantBufferByteSize = Utils::calculateConstantBufferByteSize(sizeof(MaterialConstants));
	auto passConstantBufferByteSize = Utils::calculateConstantBufferByteSize(sizeof(PassConstants));

	auto objectConstantBuffer = currentFrameResource->objectConstantBuffer->Resource();
	auto materialConstnatBuffer = currentFrameResource->materialConstantBuffer->Resource();

	// For each render item...
	for (size_t i = 0; i < renderItmes.size(); ++i)
	{
		auto renderItem = renderItmes[i];

		commandList->IASetVertexBuffers(0, 1, &renderItem->meshGeometry->VertexBufferView());
		commandList->IASetIndexBuffer(&renderItem->meshGeometry->IndexBufferView());
		commandList->IASetPrimitiveTopology(renderItem->primitiveType);

		commandList->SetGraphicsRootDescriptorTable(0, renderItem->material->textureHandle);

		// Offset to the CBV in the descriptor heap for this object and for this frame resource.
		auto objectConstantBufferViewIndex = currentFrameResourceIndex * objectCount + renderItem->objectConstantBufferIndex + textureCount;
		auto objectConstantBufferViewHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(GPU_DESCRIPTOR_HEAP_START(shaderResourceViewDescriptorHeap));
		objectConstantBufferViewHandle.Offset(objectConstantBufferViewIndex, SRVCBVUAVDescriptorSize);

		commandList->SetGraphicsRootDescriptorTable(1, objectConstantBufferViewHandle);

		// Offset to the material CBV in the descriptor heap for this object and for this frame resource.
		auto materialConstantBufferViewIndex = currentFrameResourceIndex * materialCount + renderItem->material->materialConstantBufferIndex + objectCount * NumFrameResources + textureCount;
		auto materialConstantBufferViewHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(GPU_DESCRIPTOR_HEAP_START(shaderResourceViewDescriptorHeap));
		materialConstantBufferViewHandle.Offset(materialConstantBufferViewIndex, SRVCBVUAVDescriptorSize);

		commandList->SetGraphicsRootDescriptorTable(2, materialConstantBufferViewHandle);

		commandList->DrawIndexedInstanced(renderItem->indexCount, 1, renderItem->startIndexLocation, renderItem->baseVertexLocation, 0);
	}
}

void D3DApp::waitCommandListComplete()
{
	//开始同步GPU与CPU的执行，先记录围栏标记值
	fenceValue++;

	DXCheck(commandQueue->Signal(fence.Get(), fenceValue), L"ID3D12CommandQueue::Signal failed!");

	// 看命令有没有真正执行到围栏标记的这里，没有就利用事件去等待，注意使用的是命令队列对象的指针
	if (fence->GetCompletedValue() < fenceValue)
	{
		DXCheck(fence->SetEventOnCompletion(fenceValue, fenceEvent), L"ID3D12Fence::SetEventOnCompletion failed!");
		WaitForSingleObject(fenceEvent, INFINITE);
	}
}

void D3DApp::registerGLFWEventCallbacks()
{
	glfwSetWindowUserPointer(window, static_cast<void*>(this));
	glfwSetFramebufferSizeCallback(window, onWindowResize);
	glfwSetWindowSizeCallback(window, onFramebufferResize);
	glfwSetWindowIconifyCallback(window, onWindowMinimize);
	glfwSetWindowMaximizeCallback(window, onWindowMaximize);
	glfwSetWindowFocusCallback(window, onWindowFocused);

	glfwSetCursorPosCallback(window, onMouseMove);
	glfwSetMouseButtonCallback(window, onMouseButton);
	glfwSetScrollCallback(window, onMouseWheel);
	glfwSetKeyCallback(window, onKeyDown);
}

void D3DApp::toggleWireframe()
{
	wireframe = !wireframe;
}

float D3DApp::getHillsHeight(float x, float z) const
{
	return 0.3f * (z * std::sinf(0.1f * x) + x * std::cosf(0.1f * z));
}

void D3DApp::onFramebufferResize(struct GLFWwindow* inWindow, int width, int height)
{
	auto app = static_cast<D3DApp*>(glfwGetWindowUserPointer(inWindow));

	if (width > 0 && height > 0)
	{
		app->windowWidth = width;
		app->windowHeight = height;
	}

	// Minimized
	if (app->minimized)
	{
		app->appPaused = true;
		app->minimized = true;
		app->maximized = false;
	}
	else if (app->maximized)
	{
		app->appPaused = false;
		app->maximized = false;
		app->preprareResize();
		app->onResize();
	}
	// Restore from minimized
	else if (app->restore)
	{
		if (app->minimized)
		{
			app->appPaused = false;
			app->minimized = false;
			app->preprareResize();
			app->onResize();
		}
		// Restoring from maximized state?
		else if (app->maximized)
		{
			app->appPaused = false;
			app->maximized = false;
			app->preprareResize();
			app->onResize();
		}
		else if (app->resizing)
		{

		}
		else
		{
			app->preprareResize();
			app->onResize();
		}
	}
}

void D3DApp::onWindowResize(GLFWwindow* inWindow, int width, int height)
{
	auto app = static_cast<D3DApp*>(glfwGetWindowUserPointer(inWindow));
	app->maximized = glfwGetWindowAttrib(inWindow, GLFW_MAXIMIZED);
}

void D3DApp::onWindowMinimize(GLFWwindow* inWindow, int iconified)
{
	auto app = static_cast<D3DApp*>(glfwGetWindowUserPointer(inWindow));

	if (iconified)
	{
		app->minimized = true;
		app->maximized = false;
		app->restore = -1;
	}
	else
	{
		app->restore = 1;
		app->minimized = false;
	}
}

void D3DApp::onWindowMaximize(GLFWwindow* inWindow, int maximized)
{
	auto app = static_cast<D3DApp*>(glfwGetWindowUserPointer(inWindow));

	if (maximized)
	{
		app->maximized = true;
	}
}

void D3DApp::onWindowFocused(GLFWwindow* inWindow, int focused)
{
	auto app = static_cast<D3DApp*>(glfwGetWindowUserPointer(inWindow));

	if (focused)
	{
		app->appPaused = false;
	}
	else
	{
		app->appPaused = true;
	}
}

void D3DApp::onMouseMove(GLFWwindow* inWindow, double xpos, double ypos)
{
	ImGui_ImplGlfw_CursorPosCallback(inWindow, xpos, ypos);

	auto app = static_cast<D3DApp*>(glfwGetWindowUserPointer(inWindow));

	auto xoffset = xpos - app->lastMousePosition.x;
	auto yoffset = app->lastMousePosition.y - ypos;

	if (app->rightMouseButtonDown)
	{
		app->camera.ProcessMouseMovement(static_cast<float>(xoffset), static_cast<float>(yoffset));
	}

	if (app->middleMouseButtonDown)
	{
		app->camera.ProcessKeyboard(UP, static_cast<float>(yoffset * app->FrameTime));
		app->camera.ProcessKeyboard(RIGHT, static_cast<float>(xoffset * app->FrameTime));
	}

	app->lastMousePosition.x = static_cast<float>(xpos);
	app->lastMousePosition.y = static_cast<float>(ypos);
}

void D3DApp::onMouseButton(struct GLFWwindow* inWindow, int button, int action, int mods)
{
	ImGui_ImplGlfw_MouseButtonCallback(inWindow, button, action, mods);

	auto app = static_cast<D3DApp*>(glfwGetWindowUserPointer(inWindow));

	if (button == GLFW_MOUSE_BUTTON_2 && action == GLFW_PRESS)
	{
		app->rightMouseButtonDown = true;
	}

	if (button == GLFW_MOUSE_BUTTON_2 && action == GLFW_RELEASE)
	{
		app->rightMouseButtonDown = false;
	}

	if (button == GLFW_MOUSE_BUTTON_3 && action == GLFW_PRESS)
	{
		app->middleMouseButtonDown = true;
	}

	if (button == GLFW_MOUSE_BUTTON_3 && action == GLFW_RELEASE)
	{
		app->middleMouseButtonDown = false;
	}
}

void D3DApp::onMouseWheel(GLFWwindow* inWindow, double xoffset, double yoffset)
{
	ImGui_ImplGlfw_ScrollCallback(inWindow, xoffset, yoffset);

	auto app = static_cast<D3DApp*>(glfwGetWindowUserPointer(inWindow));

	app->camera.ProcessMouseScroll(static_cast<float>(yoffset));
}

void D3DApp::onKeyDown(GLFWwindow* inWindow, int key, int scancode, int action, int mods)
{
	ImGui_ImplGlfw_KeyCallback(inWindow, key, scancode, action, mods);

	auto app = static_cast<D3DApp*>(glfwGetWindowUserPointer(inWindow));

	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
	{
		glfwSetWindowShouldClose(inWindow, true);
	}

	if (key == GLFW_KEY_SPACE && action == GLFW_PRESS)
	{
		app->toggleWireframe();
	}
}

void D3DApp::preprareResize()
{
	// Flush before changing any resources.
	// 缩放交换链之前，要等待命令队列执行完成
	// 并且要把当前使用中的命令列表分配器重置一下(因为命令列表引用的旧资源已经销毁)
	flushCommandQueue();
	resetFrameResourceCommandAllocator();
}

void D3DApp::onResize()
{
	assert(device);
	assert(swapChain);
	assert(commandAllocatorDirectPre);

	// Release the previous resources we will be recreating.
	for (int i = 0; i < FrameBackbufferCount; ++i)
	{
		renderTargets[i].Reset();
	}
	depthStencilBuffer.Reset();

	// Resize the swap chain.
	DXCheck(swapChain->ResizeBuffers(
		FrameBackbufferCount,
		windowWidth, windowHeight,
		swapChainFormat,
		DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH), L"IDXGISwapChain::ResizeBuffers failed!");

	currentFrameIndex = 0;

	createRenderTargetView();
	createDepthStencilView();

	// Execute the resize commands.
	executeCommandLists();

	// Wait until resize is complete.
	flushCommandQueue();

	// Update the viewport transform to cover the client area.
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	viewport.Width = static_cast<float>(windowWidth);
	viewport.Height = static_cast<float>(windowHeight);
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	scissorRect = { 0, 0, windowWidth, windowHeight };
}

void D3DApp::onRenderTextureResize(uint32_t width, uint32_t height)
{
	// Flush before changing any resources.
	flushCommandQueue();

	renderTexture.Reset();
	renderTextureDepthStencilBuffer.Reset();

	createRenderTextureRTV(width, height);
	createRenderTextureDSV(width, height);
	createRenderTextureSRV();
}

void D3DApp::disableAlphaBlend()
{
	commandListDirectPre->SetPipelineState(pipelineStates["NoAlpha"].Get());
}

