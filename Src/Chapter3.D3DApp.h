#pragma once

#include <wrl.h>

using namespace Microsoft;
using namespace Microsoft::WRL;

#include <dxgi1_6.h>
#include <DirectXMath.h>

using namespace DirectX;

#include <d3d12.h>
#include <d3d12shader.h>
#include <d3dcompiler.h>

#if defined(_DEBUG)
#include <dxgidebug.h>
#endif

#include <d3dx12.h>

#include <backends/imgui_impl_dx12.h>
#include <backends/imgui_impl_win32.h>

#define MAX_LOADSTRING 100

struct DXVertex
{
	XMFLOAT3 position;
	XMFLOAT2 texcood;
	XMFLOAT4 color;
};

class D3DApp
{
public:
	D3DApp();

	bool initialize();

	static D3DApp* getApp();
	LRESULT msgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

	int32_t run();
private:
	ATOM MyRegisterClass(HINSTANCE hInstance);
	BOOL InitInstance(HINSTANCE hInstance);

	bool initWindow();

	void createDebugLayer();
	void createDXGIFactory();
	void createDevice();
	void createCommandQueue();
	void createSwapChain();
	void createDescriptorHeap();
	void createRenderTargetView();
	void createSamplerDescriptorHeap();
	void createSamplers();
	void createRootSignature();
	void createGraphicsPipelineState();
	void createVertexBuffer();
	void createCommandList();
	void createFence();

	void initDirect3D();

	void loadResources();

	void initImGui();
	void updateImGui();
	void createImGuiWidgets();
	void renderImGui();
	void shutdownImGui();

	void update();
	void processInput(float deltaTime);
	void render();

private:
	const static uint32_t frameBackbufferCount = 3;
	int windowWidth = 1600;
	int windowHeight = 900;

	bool quit = false;

	uint32_t frameIndex = 0;
	uint32_t frame = 0;

	uint32_t DXGIFactoryFlags = 0;
	uint32_t renderTargetViewDescriptorSize = 0;
	uint32_t samplerDescriptorSize = 0;

	uint32_t currentSamplerNo = 0;		//当前使用的采样器索引
	uint32_t sampleMaxCount = 5;		//创建五个典型的采样器

	HWND mainWindow = nullptr;

	float aspectRatio = static_cast<float>(windowWidth) / static_cast<float>(windowHeight);

	D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};

	UINT64 fenceValue = 0;
	HANDLE fenceEvent = nullptr;

	CD3DX12_VIEWPORT viewport{ 0.0f, 0.0f, static_cast<float>(windowWidth), static_cast<float>(windowHeight) };
	CD3DX12_RECT scissorRect{ 0, 0, static_cast<long>(windowWidth), static_cast<long>(windowHeight) };

	DXGI_FORMAT swapChainFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

	uint32_t vertexCount = 0;

	ComPtr<IDXGIFactory7> DXGIFactory;
	ComPtr<IDXGIAdapter1> DXGIAdapter;
	ComPtr<ID3D12Device4> device;
	ComPtr<ID3D12CommandQueue> commandQueue;
	ComPtr<IDXGISwapChain1> swapChain1;

	// DXR stuff
	ComPtr<IDXGISwapChain4> swapChain;
	//ComPtr<IDXGISwapChain3> swapChain;
	ComPtr<ID3D12DescriptorHeap> renderTargetViewDescriptorHeap;
	ComPtr<ID3D12DescriptorHeap> imGuiShaderResourceViewDescriptorHeap;
	ComPtr<ID3D12DescriptorHeap> shaderResourceViewDescriptorHeap;
	ComPtr<ID3D12DescriptorHeap> samplerDescriptorHeap;
	ComPtr<ID3D12Resource> renderTargets[frameBackbufferCount];
	ComPtr<ID3D12CommandAllocator> commandAllocator;
	ComPtr<ID3D12RootSignature> rootSignature;
	ComPtr<ID3D12PipelineState> graphicsPipelineState;
	ComPtr<ID3D12GraphicsCommandList> commandList;
	ComPtr<ID3D12Resource> vertexBuffer;
	ComPtr<ID3D12Fence> fence;
	ComPtr<ID3D12Resource> texture;
	ComPtr<ID3D12Heap> textureHeap;
	ComPtr<ID3D12Heap> uploadHeap;

	// Our state
	bool showDemoWindow = true;
	bool showAnotherWindow = false;
	ImVec4 clearColor = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

	static D3DApp* app;

	// Global Variables:
	WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
	WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name
};