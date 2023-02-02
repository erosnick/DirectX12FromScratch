#pragma once

#include <chrono>
#include <wrl.h>

using namespace Microsoft;
using namespace Microsoft::WRL;

#include <dxgi1_6.h>

#include <d3d12.h>
#include <d3d12shader.h>
#include <d3dcompiler.h>

#if defined(_DEBUG)
#include <dxgidebug.h>
#endif

#include <d3dx12.h>

#define IMGUI_UNLIMITED_FRAME_RATE
#include <backends/imgui_impl_dx12.h>
#include <backends/imgui_impl_win32.h>

#include "Model.h"
#include "Camera.h"

#define MAX_LOADSTRING 100

struct ModelViewProjectionBuffer
{
	//XMFLOAT4X4 model;
	//XMFLOAT4X4 modelViewProjection;
	glm::mat4 model;
	glm::mat4 modelViewProjection;
	//glm::mat4 testMatrix;
};

class D3DApp
{
public:
	D3DApp();

	bool initialize(HINSTANCE hInstance, int32_t cmdShow);

	static D3DApp* getApp();
	LRESULT msgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

	int32_t run();
private:
	ATOM MyRegisterClass(HINSTANCE hInstance);
	BOOL InitInstance(HINSTANCE hInstance, int32_t cmdShow);

	bool initWindow(HINSTANCE hInstance, int32_t cmdShow);

	void createDebugLayer();
	void createDXGIFactory();
	void createDevice();
	void createCommandQueue();
	void createSwapChain();
	void createDescriptorHeap();
	void createRenderTargetView();
	void createSkyboxDescriptorHeap();
	void createSkyboxDescriptors();
	void createRootSignature();
	void createGraphicsPipelineState();
	void createSkyboxGraphicsPipelineState();
	void createCommandList();
	void createFence();
	void createUploadHeap(uint64_t heapSize);
	void loadCubeResource();
	void loadResources();
	void loadSkyboxTexture();
	void createVertexBuffer(const DXModel& model);
	void createIndexBuffer(const DXModel& model);
	void createSkyboxVertexBuffer(const DXModel& model);
	void createSkyboxIndexBuffer(const DXModel& model);
	void createConstantBufferView();
	void createSkyboxConstantBufferView();
	void createDepthStencilDescriptorHeap();
	void createDepthStencilBuffer();
	void createSamplerDescriptorHeap();
	void createSamplers();
	void createSkyboxSamplerDescriptorHeap();
	void createSkyboxSampler();
	void recordCommands();

	void initDirect3D();

	void initImGui();
	void updateImGui();
	void createImGuiWidgets();
	void renderImGui();
	void shutdownImGui();

	void updateConstantBuffer();
	void updateSkyboxConstantBuffer();
	void updateFPSCounter();
	void update();
	void processInput(float deltaTime);
	void render();

	void onMouseMove(float x, float y);
	void onMouseWheel(float offset);

private:
	const static uint32_t FrameBackbufferCount = 3;
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
	D3D12_INDEX_BUFFER_VIEW indexBufferView{};

	D3D12_VERTEX_BUFFER_VIEW skyboxVertexBufferView{};
	D3D12_INDEX_BUFFER_VIEW skyboxIndexBufferView{};

	uint64_t fenceValue = 0;
	HANDLE fenceEvent = nullptr;

	CD3DX12_VIEWPORT viewport{ 0.0f, 0.0f, static_cast<float>(windowWidth), static_cast<float>(windowHeight) };
	CD3DX12_RECT scissorRect{ 0, 0, static_cast<long>(windowWidth), static_cast<long>(windowHeight) };

	DXGI_FORMAT swapChainFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

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
	ComPtr<ID3D12DescriptorHeap> depthStencilViewDescriptorHeap;
	ComPtr<ID3D12Resource> renderTargets[FrameBackbufferCount];
	ComPtr<ID3D12CommandAllocator> commandAllocatorDirect;
	ComPtr<ID3D12CommandAllocator> commandAllocatorSkybox;
	ComPtr<ID3D12CommandAllocator> commandAllocatorScene;
	ComPtr<ID3D12RootSignature> rootSignature;
	ComPtr<ID3D12PipelineState> graphicsPipelineState;
	ComPtr<ID3D12PipelineState> skyboxGraphicsPipelineState;
	ComPtr<ID3D12GraphicsCommandList> commandListDirect;
	ComPtr<ID3D12GraphicsCommandList> skyboxBundle;
	ComPtr<ID3D12GraphicsCommandList> sceneBundle;
	ComPtr<ID3D12Resource> vertexBuffer;
	ComPtr<ID3D12Resource> indexBuffer;
	ComPtr<ID3D12Resource> constantBuffer;
	ComPtr<ID3D12Resource> depthStencilBuffer;
	ComPtr<ID3D12Fence> fence;
	ComPtr<ID3D12Resource> texture;
	ComPtr<ID3D12Heap> textureHeap;
	ComPtr<ID3D12Heap> uploadHeap;

	uint32_t skyboxSamplerDescriptorSize = 0;

	ComPtr<ID3D12DescriptorHeap> skyboxDescriptorHeap;
	ComPtr<ID3D12DescriptorHeap> skyboxSamplerDescriptorHeap;

	ComPtr<ID3D12Heap> skyboxUploadHeap;
	ComPtr<ID3D12Resource> skyboxTexture;
	ComPtr<ID3D12Resource> skyboxTextureUploadBuffer;
	ComPtr<ID3D12Resource> skyboxConstantBuffer;
	ComPtr<ID3D12Resource> skyboxVertexBuffer;
	ComPtr<ID3D12Resource> skyboxIndexBuffer;

	// Our state
	bool showDemoWindow = true;
	bool showAnotherWindow = false;
	ImVec4 clearColor = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

	static D3DApp* app;

	// Global Variables:
	WCHAR szTitle[MAX_LOADSTRING] {};                  // The title bar text
	WCHAR szWindowClass[MAX_LOADSTRING];			   // the main window class name

	// 初始的默认摄像机的位置

	float angularVelocity = 10.0f * glm::pi<float>() / 180.0f; // 物体旋转的角速度，单位：弧度/秒

	float boxSize = 3.0f;
	float texcoordScale = 3.0f;

	uint64_t textureUploadBufferSize = 0;
	uint64_t skyboxTextureUploadBufferSize = 0;
	uint32_t vertexBufferSize = 0;
	uint32_t indexBufferSize = 0;
	uint32_t skyboxVertexBufferSize = 0;
	uint32_t skyboxIndexBufferSize = 0;
	uint64_t bufferOffset = 0;
	uint64_t skyboxBufferOffset = 0;
	uint32_t constantBufferSize = 0;
	uint32_t skyboxConstantBufferSize = 0;

	uint32_t vertexCount = 0;
	uint32_t indexCount = 0;
	uint32_t skyboxIndexCount = 0;
	uint32_t triangleCount = 0;

	float modelRotationYAngle = 0.0f;

	float startTime = 0.0f;

	const float FrameTime = 0.0166667f;

	bool renderReady = false;

	uint32_t frameCount = 0;

	glm::vec2 lastMousePosition;
	bool rightMouseButtonDown = false;

	ModelViewProjectionBuffer* modelViewProjectionBuffer;
	ModelViewProjectionBuffer* skyboxModelViewProjectionBuffer;

	DXModel cubeModel;
	DXModel bunny;
	DXModel skybox;

	// 初始的默认摄像机的位置
	Camera camera{ { 0.0f, 1.0f, -5.0f } };
};