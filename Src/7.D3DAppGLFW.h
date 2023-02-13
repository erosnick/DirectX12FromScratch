#pragma once

#include <chrono>
#include <memory>
#include <wrl.h>

using namespace Microsoft;
using namespace Microsoft::WRL;

#include <dxgi1_6.h>

#include <d3d12.h>
#include <dxcapi.h>
#include <d3d12shader.h>

#if defined(_DEBUG)
#include <dxgidebug.h>
#endif

#include <d3dx12.h>

#include "ImGuiLayer.h"

#include "Model.h"
#include "Camera.h"
#include "GameTimer.h"
#include "FrameResource.h"

// Lightweight structure stores parameters to draw a shape.  This will
// vary from app-to-app.
struct RenderItem
{
	RenderItem() = default;

	// World matrix of the shape that describes the object's local space
	// relative to the world space, which defines the position, orientation,
	// and scale of the object in the world.
	glm::mat4 model = glm::mat4(1.0f);

	// Dirty flag indicating the object data has changed and we need to update the constant buffer.
	// Because we have an object cbuffer for each FrameResource, we have to apply the
	// update to each FrameResource.  Thus, when we modify object data we should set 
	// NumFramesDirty = gNumFrameResources so that each frame resource gets the update.
	uint32_t NumFramesDirty = NumFrameResources;

	// Index into GPU constant buffer corresponding to the ObjectCB for this render item.
	uint32_t objectConstantBufferIndex = -1;

	MeshGeometry* meshGeometry = nullptr;

	// Primitive topology.
	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	// DrawIndexedInstanced parameters.
	uint32_t indexCount = 0;
	uint32_t startIndexLocation = 0;
	uint32_t baseVertexLocation = 0;
};

// ��Ⱦ���̲߳���
struct RenderThreadPayload
{
	uint32_t index = 0;		// ���
	uint32_t threadID = 0;
	HANDLE threadHandle = nullptr;
	DWORD mainThreadID = 0;
	HANDLE mainThreadHandle = nullptr;
	HANDLE runEvent = nullptr;
	HANDLE eventRenderOver = nullptr;
	uint32_t currentFrameIndex = 0;
	double startTime = 0.0f;
	double currentTime = 0.0f;
	std::wstring ddsFile = L"";
	std::string objFile = "";
	glm::vec3 position{ 0.0f };
	glm::vec3 scale{ 1.0f };
	glm::mat4 view;
	glm::mat4 projection;
	ComPtr<ID3D12Device4> device;
	ComPtr<ID3D12CommandAllocator> commandAllocator;
	ComPtr<ID3D12GraphicsCommandList> commandList;
	ComPtr<ID3D12RootSignature> rootSignature;
	ComPtr<ID3D12PipelineState> pipelineState;
	ComPtr<ID3D12DescriptorHeap> renderTargetViewDescriptorHeap;
	ComPtr<ID3D12DescriptorHeap> depthStencilViewDescriptorHeap;
	CD3DX12_VIEWPORT viewport{};
	CD3DX12_RECT scissorRect{};
	uint32_t renderTargetViewDescriptorSize = 0;
	bool isCubemap = false;
};

const uint32_t MaxThreadCount = 3;
const uint32_t SkyboxThreadIndex = 0;
const uint32_t SceneObjectThreadIndex1 = 1;
const uint32_t SceneObjectThreadIndex2 = 2;

static RenderThreadPayload renderThreadPayloads[MaxThreadCount];

class D3DApp
{
public:
	D3DApp();

	bool initialize(HINSTANCE hInstance, int32_t cmdShow);

	static D3DApp* getApp();

	static uint32_t __stdcall renderThread(void* data);

	int32_t run();
	int32_t runMultithread();

	void renderImGui();

private:
	bool initGLFW();
	bool initWindow(HINSTANCE hInstance, int32_t cmdShow);

	void createDebugLayer();
	void createDXGIFactory();
	void createDevice();
	void createCommandQueue();
	void createSwapChain();
	void createShaderResourceViewDescriptorHeap();
	void createRenderTargetView();
	void createSkyboxDescriptorHeap();
	void createSkyboxDescriptors();
	void createRootSignature();
	void createGraphicsPipelineState();
	void createSkyboxGraphicsPipelineState();
	void createCommandLists();
	void createFence();
	void createUploadHeap(uint64_t heapSize);
	void loadCubeResource();
	void loadResources();

	void flushCommandQueue();

	void loadSkyboxTexture();
	void createRenderTextureRTVDescriptorHeap();
	void createRenderTextureRTV(uint32_t width, uint32_t height);
	void createRenderTextureSRVDescriptorHeap();
	void createRenderTextureSRV();
	void createRenderTextureDSVDescriptorHeap();
	void createRenderTextureDSV(uint32_t width, uint32_t height);

	/// ���ڴ������㻺�����İ�������
	///
	/// \param model: ���ص�.objģ������
	/// \param heap: ���ڴ������㻺�����Ķ�
	/// \param offset: �ڶ���λ�õ�ƫ��
	/// \param vertexBuffer: ���㻺������
	/// \param vertexBufferView: ���㻺����ͼ
	void createVertexBuffer(const DXModel& model, const ComPtr<ID3D12Heap>& heap, uint64_t offset, 
							ComPtr<ID3D12Resource>& vertexBuffer, D3D12_VERTEX_BUFFER_VIEW& vertexBufferView);

	void createVertexBuffer(const DXModel& model);

	/// ���ڴ��������������İ�������
	///
	/// \param model: ���ص�.objģ������
	/// \param heap: ���ڴ��������������Ķ�
	/// \param offset: �ڶ���λ�õ�ƫ��
	/// \param indexBuffer: ������������
	/// \param indexBufferView: ����������ͼ
	void createIndexBuffer(const DXModel& model, const ComPtr<ID3D12Heap>& heap, uint64_t offset,
							ComPtr<ID3D12Resource>& indexBuffer, D3D12_INDEX_BUFFER_VIEW& indexBufferView);

	void createIndexBuffer(const DXModel& model);
	void createSkyboxVertexBuffer(const DXModel& model);
	void createSkyboxIndexBuffer(const DXModel& model);

	/// ���ڴ���������������ͼ�İ�������
	///
	/// \param heap: ���ڴ��������������Ķ�
	/// \param offset: �ڶ���λ�õ�ƫ��
	/// \param size: ������������С
	/// \param descriptorHeap: ���ڴ���������������ͼ����������
	/// \param index: �����������е�����
	/// \param constantBuffer: ������������
	/// \param indexBufferView: ����������ͼ
	/// \param modelViewProjectionBuffer: ӳ�䵽CPU�ĳ���������ָ��
	void createConstantBufferView(const ComPtr<ID3D12Heap>& heap, 
								  uint64_t offset, uint32_t size, 
								  const ComPtr<ID3D12DescriptorHeap>& descriptorHeap,
								  uint32_t index,
								  ComPtr<ID3D12Resource>& constantBuffer, 
								  ObjectConstants** modelViewProjectionBuffer);

	void createConstantBufferView();
	void createSkyboxConstantBufferView();
	void createRenderTextureConstantBufferView();
	void createDepthStencilDescriptorHeap();
	void createDepthStencilView();
	void createSamplerDescriptorHeap();
	void createSamplers();
	void createSkyboxSamplerDescriptorHeap();
	void createSkyboxSampler();
	void recordCommands();
	void prepareRenderThreads();
	void createRenderItems();

	/// ���ڴ��������б�������İ�������
	/// \param type: �����б����������
	/// \param commandAllocator: �����õ������б������
	/// \param name: ��ѡ�������б��������������
	/// \param index: ��ѡ�ĵ�����������(���紴�������ʱ��)
	void createCommandAllocator(D3D12_COMMAND_LIST_TYPE type, ComPtr<ID3D12CommandAllocator>& commandAllocator, 
								const std::wstring& name = L"", uint32_t index = 0);
	void createCommandAllocator(D3D12_COMMAND_LIST_TYPE type, ID3D12CommandAllocator** commandAllocator, 
								const std::wstring& name = L"", uint32_t index = 0);

	/// ���ڴ��������б�İ�������
	/// \param type: �����б�����
	/// \param commandAllocator: �����б������
	/// \param pipelineState: ��ʼ��ͼ����Ⱦ����״̬����(ִ�з�ͼ����Ⱦ����ʱ����Ϊnullptr)
	/// \param commandList: �����õ������б������
	/// \param name: ��ѡ�������б��������������
	/// \param index: ��ѡ�ĵ�����������(���紴�������ʱ��)
	void createCommandList(D3D12_COMMAND_LIST_TYPE type, const ComPtr<ID3D12CommandAllocator>& commandAllocator,
														 const ComPtr<ID3D12PipelineState>& pipelineState, 
														 ComPtr<ID3D12GraphicsCommandList>& commandList, 
														 const std::wstring& name = L"", uint32_t index = 0);
	void createCommandList(D3D12_COMMAND_LIST_TYPE type, ID3D12CommandAllocator* commandAllocator,
														 const ComPtr<ID3D12PipelineState>& pipelineState,
														 ID3D12GraphicsCommandList** commandList,
														 const std::wstring& name = L"", uint32_t index = 0);

	/// ���ڴ������/ģ�建����ͼ�İ�������
	/// \param width: ���/ģ�建�������
	/// \param height: ���/ģ�建�����߶�
	/// \param format: ���/ģ�建������ʽ
	/// \param depthStencilDescriptorHeap: ���ڴ������/ģ�建����ͼ����������
	/// \param depthStencilBuffer: ���/ģ�建����
	void createDepthStencilView(uint32_t width, uint32_t height, DXGI_FORMAT format,
								const ComPtr<ID3D12DescriptorHeap>& depthStencilDescriptorHeap, 
								ComPtr<ID3D12Resource>& depthStencilBuffer);

	/// ���ڴ����������ѵİ�������
	/// \param type: ������������
	/// \param numDescriptors: �������Ѱ���������������
	/// \param shaderVisible: �Ƿ���ΪShader�ɼ���Դ
	/// \param descriptorHeap: �����õ���������
	/// \param name: ��ѡ���������ѵ�������
	void createDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors, bool shaderVisible, ComPtr<ID3D12DescriptorHeap>& descriptorHeap, const std::wstring& name = L"");
	ComPtr<ID3D12DescriptorHeap> createDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors, bool shaderVisible, const std::wstring& name = L"");

	/// ���ڴ����������İ�������
	/// \param filter: ����������
	/// \param addressMode: Ѱַģʽ 
	/// \param descriptorHeap: ���ڴ�������������������
	/// \param index: �����������е�����
	void createSampler(D3D12_FILTER filter, D3D12_TEXTURE_ADDRESS_MODE addressMode, const ComPtr<ID3D12DescriptorHeap>& descriptorHeap, uint32_t index = 0);

	/// ���ڴ���SRV�İ�������
	/// \param dimension: SRV��ά��(1D, 2D, CUBE)
	/// \param texture: ���ڴ���SRV��������Դ
	/// \param descriptorHeap: ���ڴ���SRV����������
	/// \param index: �����������е�����
	void createShaderResourceView(D3D12_SRV_DIMENSION dimension, const ComPtr<ID3D12Resource>& texture, const ComPtr<ID3D12DescriptorHeap>& descriptorHeap, uint32_t index = 0);

	std::shared_ptr<struct MeshGeometry> createMeshGeometry(const DXModel& model);

	void initializeDirect3D();

	void initImGui();
	void updateImGui();
	void onGUIRender();
	void renderImGui(const ComPtr<ID3D12GraphicsCommandList>& commandList);
	void shutdownImGui();

	void updateConstantBuffer();
	void updateSkyboxConstantBuffer();
	void calculateFrameStats();
	void update();
	void processInput(float deltaTime);
	void render();
	void drawRenderItems(const ComPtr<ID3D12GraphicsCommandList>& commandList, const std::vector<RenderItem*>& renderItmes);

	void waitCommandListComplete();

	void registerGLFWEventCallbacks();

	static void onFramebufferResize(struct GLFWwindow* inWindow, int width, int height);
	static void onWindowResize(struct GLFWwindow* inWindow, int width, int height);
	static void onWindowMinimize(GLFWwindow* inWindow, int iconified);
	static void onWindowMaximize(GLFWwindow* inWindow, int maximized);
	static void onWindowFocused(GLFWwindow* inWindow, int focused);
	static void onMouseMove(struct GLFWwindow* inWindow, double xpos, double ypos);
	static void onMouseButton(struct GLFWwindow* inWindow, int button, int action, int mods);
	static void onMouseWheel(GLFWwindow* inWindow, double xoffset, double yoffset);
	static void onKeyDown(GLFWwindow* inWindow, int key, int scancode, int action, int mods);

	void onResize();
	void onRenderTextureResize(uint32_t width, uint32_t height);
private:
	const static uint32_t FrameBackbufferCount = 3;
	int32_t windowWidth = 1920;
	int32_t windowHeight = 1080;
	int32_t viewportWidth = 800;
	int32_t viewportHeight = 600;
	int32_t imGuiWindowWidth = 800;
	int32_t imGuiWindowHeight = 600;

	const float StandardAspectRatio = 16.0f / 9.0f;
	float aspectRatio = static_cast<float>(windowWidth) / 
						static_cast<float>(windowHeight);

	bool quit = false;

	uint32_t currentFrameIndex = 0;
	uint32_t imGuiCurrentFrameIndex = 0;
	uint32_t frame = 0;

	std::vector<std::unique_ptr<FrameResource>> frameResources;
	FrameResource* currentFrameResource;
	uint32_t currentFrameResourceIndex = 0;

	// List of all the render items.
	std::vector<std::unique_ptr<RenderItem>> allRenderItems;

	// Render items divided by PSO.
	std::vector<RenderItem*> opaqueRenderItems;

	uint32_t DXGIFactoryFlags = 0;
	uint32_t renderTargetViewDescriptorSize = 0;
	uint32_t depthStencilViewDescriptorSize = 0;
	uint32_t SRVCBVUAVDescriptorSize = 0;
	uint32_t samplerDescriptorSize = 0;

	uint32_t currentSamplerNo = 0;		//��ǰʹ�õĲ���������
	uint32_t sampleMaxCount = 5;		//����������͵Ĳ�����

	HWND mainWindow = nullptr;

	D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
	D3D12_INDEX_BUFFER_VIEW indexBufferView{};

	D3D12_VERTEX_BUFFER_VIEW skyboxVertexBufferView{};
	D3D12_INDEX_BUFFER_VIEW skyboxIndexBufferView{};

	uint64_t fenceValue = 0;

	HANDLE fenceEvent;

	CD3DX12_VIEWPORT viewport{ 0.0f, 0.0f, static_cast<float>(windowWidth), static_cast<float>(windowHeight) };
	CD3DX12_RECT scissorRect{ 0, 0, static_cast<long>(windowWidth), static_cast<long>(windowHeight) };

	CD3DX12_VIEWPORT imGuiViewport{ 0.0f, 0.0f, static_cast<float>(windowWidth), static_cast<float>(windowHeight) };
	CD3DX12_RECT imGuiScissorRect{ 0, 0, static_cast<long>(viewportWidth), static_cast<long>(windowHeight) };

	DXGI_FORMAT swapChainFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	DXGI_FORMAT depthStencilFormat = DXGI_FORMAT_D32_FLOAT;

	ComPtr<IDXGIFactory7> DXGIFactory;
	ComPtr<IDXGIAdapter1> DXGIAdapter;
	ComPtr<ID3D12Device4> device;
	ComPtr<ID3D12CommandQueue> commandQueue;
	ComPtr<IDXGISwapChain1> swapChain1;

	// DXR stuff
	ComPtr<IDXGISwapChain4> swapChain;
	ComPtr<IDXGISwapChain4> imGuiSwapChain;
	//ComPtr<IDXGISwapChain3> swapChain;
	ComPtr<ID3D12DescriptorHeap> renderTargetViewDescriptorHeap;
	ComPtr<ID3D12DescriptorHeap> renderTextureRTVDescriptorHeap;
	ComPtr<ID3D12DescriptorHeap> shaderResourceViewDescriptorHeap;
	ComPtr<ID3D12DescriptorHeap> renderTextureSRVDescriptorHeap;
	ComPtr<ID3D12DescriptorHeap> imGuiShaderResourceViewDescriptorHeap;
	ComPtr<ID3D12DescriptorHeap> samplerDescriptorHeap;
	ComPtr<ID3D12DescriptorHeap> depthStencilViewDescriptorHeap;
	ComPtr<ID3D12DescriptorHeap> renderTextureDSVDescriptorHeap;
	ComPtr<ID3D12Resource> renderTargets[FrameBackbufferCount];
	ComPtr<ID3D12Resource> imGuiRenderTargets[FrameBackbufferCount];
	ComPtr<ID3D12CommandAllocator> commandAllocatorDirectPre;
	ComPtr<ID3D12CommandAllocator> commandAllocatorDirectPost;
	ComPtr<ID3D12CommandAllocator> commandAllocatorDirectImGui;
	ComPtr<ID3D12CommandAllocator> commandAllocatorSkybox;
	ComPtr<ID3D12CommandAllocator> commandAllocatorScene;
	ComPtr<ID3D12CommandAllocator> commandAllocatorRenderTextureObject;
	ComPtr<ID3D12RootSignature> rootSignature;
	ComPtr<ID3D12RootSignature> renderTextureRootSignature;
	ComPtr<ID3D12PipelineState> graphicsPipelineState;
	ComPtr<ID3D12PipelineState> skyboxGraphicsPipelineState;
	ComPtr<ID3D12PipelineState> renderTextureGraphicsPipelineState;
	ComPtr<ID3D12GraphicsCommandList> commandListDirectPre;
	ComPtr<ID3D12GraphicsCommandList> commandListDirectPost;
	ComPtr<ID3D12GraphicsCommandList> commandListDirectImGui;
	ComPtr<ID3D12GraphicsCommandList> skyboxBundle;
	ComPtr<ID3D12GraphicsCommandList> sceneBundle;
	ComPtr<ID3D12GraphicsCommandList> renderTextureObject;
	std::vector<ID3D12CommandList*> commandlists;
	ComPtr<ID3D12Resource> vertexBuffer;
	ComPtr<ID3D12Resource> indexBuffer;
	ComPtr<ID3D12Resource> constantBuffer;
	ComPtr<ID3D12Resource> renderTextureConstantBuffer;
	ComPtr<ID3D12Resource> depthStencilBuffer;
	ComPtr<ID3D12Resource> renderTexture;
	ComPtr<ID3D12Resource> renderTextureDepthStencilBuffer;
	ComPtr<ID3D12Fence> fence;
	ComPtr<ID3D12Resource> texture;
	ComPtr<ID3D12Heap> textureHeap;
	ComPtr<ID3D12Heap> uploadHeap;

	ComPtr<ID3D12DescriptorHeap> skyboxDescriptorHeap;
	ComPtr<ID3D12DescriptorHeap> skyboxSamplerDescriptorHeap;

	ComPtr<ID3D12Heap> skyboxUploadHeap;
	ComPtr<ID3D12Resource> skyboxTexture;
	ComPtr<ID3D12Resource> skyboxTextureUploadBuffer;
	ComPtr<ID3D12Resource> skyboxConstantBuffer;
	ComPtr<ID3D12Resource> skyboxVertexBuffer;
	ComPtr<ID3D12Resource> skyboxIndexBuffer;

	std::vector<HANDLE> waitedHandles;
	std::vector<HANDLE> subTheadHandles;

	// Our state
	bool showDemoWindow = true;
	bool showAnotherWindow = false;
	ImVec4 clearColor = ImVec4(0.4f, 0.6f, 0.9f, 1.00f);

	static D3DApp* app;

	// Global Variables:
	std::wstring windowTitle = L"7.Render to Texture";	// The title bar text
	std::wstring windowClass = L"DirectX12Window";	    // the main window class name

	// ��ʼ��Ĭ���������λ��

	float angularVelocity = 10.0f * glm::pi<float>() / 180.0f; // ������ת�Ľ��ٶȣ���λ������/��

	float boxSize = 3.0f;
	float texcoordScale = 3.0f;

	float modelRotationYAngle = 0.0f;

	double startTime = 0.0f;
	double currentTime = 0.0f;

	const float FrameTime = 0.01666667f;

	double simulationTime = 0;

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

	glm::vec2 lastMousePosition;

	bool appPaused = false;
	bool minimized = false;
	bool maximized = false;
	bool resizing = false;
	int32_t restore = -1;
	bool renderReady = false;
	bool rightMouseButtonDown = false;
	bool middleMouseButtonDown = false;
	bool compileOnTheFly = true;

	ObjectConstants* modelViewProjectionBuffer;
	ObjectConstants* skyboxModelViewProjectionBuffer;
	ObjectConstants* renderTextureModelViewProjectionBuffer;

	DXModel cubeModel;
	DXModel bunny;
	DXModel skybox;

	std::shared_ptr<MeshGeometry> meshGeometry;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> meshGeometries;

	// ��ʼ��Ĭ���������λ��
	Camera camera{ { 0.0f, 1.0f, -5.0f } };

	GameTimer gameTimer;

	ImGuiLayer imGuiLayer;

	glm::mat4 projection;

	struct GLFWwindow* window;
};