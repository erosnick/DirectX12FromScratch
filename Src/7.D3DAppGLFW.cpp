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
	catch (const std::exception& e)
	{
		fmt::print("{}\n", e.what());
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

			while (simulationTime < realTime)
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

int32_t D3DApp::runMultithread()
{
	MSG msg{};

	// 记录帧开始时间，和当前时间，以循环结束为界
	static auto startTimeDuration = std::chrono::high_resolution_clock::now().time_since_epoch();
	static auto simulationTime = std::chrono::duration<float, std::chrono::seconds::period>(startTimeDuration).count();
	startTime = simulationTime;
	currentTime = startTime;

	uint32_t states = 0;	// 初始状态为0
	DWORD returnValue = 0;
	std::vector<ID3D12CommandList*> commandLists;

	bool quit = false;

	// 起始的时候关闭一下两个命令列表，因为我们在开始渲染的时候都需要先Reset它们，为了防止报错故先Close
	DXCheck(commandListDirectPre->Close(), L"commandListDirectPre close failed!");
	DXCheck(commandListDirectPost->Close(), L"commandListDirectPre close failed!");
	DXCheck(commandListDirectImGui->Close(), L"commandListDirectImGui close failed!");

	// 这句为了保证MsgWaitForMultipleObjects 在 wait for all 为True时 能够及时返回
	//SetTimer(mainWindow, WM_USER + 100, 1, nullptr);

	// 渲染循环
	while (!quit)
	{
		// 注意这里我们调整了消息循环，将等待时间设置为0，同时将定时性的渲染，改成了每次循环都渲染
		// 特别注意这次等待与之前不同
		// 主线程进入等待
		auto waitCount = static_cast<DWORD>(waitedHandles.size());
		//returnValue = MsgWaitForMultipleObjects(waitCount, waitedHandles.data(), TRUE, INFINITE, QS_ALLINPUT);
		returnValue = WaitForMultipleObjects(waitCount, waitedHandles.data(), TRUE, INFINITE);

		returnValue -= WAIT_OBJECT_0;

		processInput(FrameTime);

		if (returnValue == 0)
		{
			switch (states)
			{
			// 状态0，表示等到各子线程加载资源完毕，此时执行一次命令列表完成各子线程要求的资源上传第二个copy命令
			case 0:
			{
				commandLists.clear();

				// 执行命令列表
				commandLists.push_back(renderThreadPayloads[SkyboxThreadIndex].commandList.Get());
				commandLists.push_back(renderThreadPayloads[SceneObjectThreadIndex1].commandList.Get());
				commandLists.push_back(renderThreadPayloads[SceneObjectThreadIndex2].commandList.Get());
				commandLists.push_back(commandListDirectPre.Get());

				commandQueue->ExecuteCommandLists(static_cast<uint32_t>(commandLists.size()), commandLists.data());

				// 开始同步GPU与CPU的执行，先记录围栏标记值
				const UINT64 targetFenceValue = fenceValue;

				DXCheck(commandQueue->Signal(fence.Get(), targetFenceValue), L"ID3D12CommandQueue::Signal failed!");
				fenceValue++;

				// 看命令有没有真正执行到围栏标记的这里，没有就利用事件去等待，注意使用的是命令队列对象的指针
				DXCheck(fence->SetEventOnCompletion(targetFenceValue, fenceEvent), L"ID3D12Fence::SetEventOnCompletion failed!");

				states = 1;

				waitedHandles.clear();
				waitedHandles.push_back(fenceEvent);
			}
			break;
			// 状态1，等到命令队列执行结束(也即CPU等到GPU执行结束)，开始新一轮渲染
			case 1:
			{
				DXCheck(commandAllocatorDirectPre->Reset(), L"commandAllocatorDirectPre reset failed!");
				DXCheck(commandListDirectPre->Reset(commandAllocatorDirectPre.Get(), graphicsPipelineState.Get()),
					L"commandListDirectPre reset failed!");

				DXCheck(commandAllocatorDirectPost->Reset(), L"commandAllocatorDirectPost reset failed!");
				DXCheck(commandListDirectPost->Reset(commandAllocatorDirectPost.Get(), graphicsPipelineState.Get()),
					L"commandListDirectPost reset failed!");

				DXCheck(commandAllocatorDirectImGui->Reset(), L"commandAllocatorDirectImGui reset failed!");
				DXCheck(commandListDirectImGui->Reset(commandAllocatorDirectImGui.Get(), graphicsPipelineState.Get()),
					L"commandAllocatorDirectImGui reset failed!");

				currentFrameIndex = swapChain->GetCurrentBackBufferIndex();

				// 计算全局的matrix
				auto model = glm::mat4(1.0f);
				auto view = camera.GetViewMatrix();
				auto projection = glm::perspective(glm::radians(camera.Zoom), static_cast<float>(windowWidth) / static_cast<float>(windowHeight), 0.1f, 100.0f);

				auto modelViewProjection = projection * view * model;

				modelViewProjectionBuffer->model = model;
				modelViewProjectionBuffer->modelViewProjection = modelViewProjection;

				auto currentTimeDuration = std::chrono::high_resolution_clock::now().time_since_epoch();
				currentTime = std::chrono::duration<float, std::chrono::seconds::period>(currentTimeDuration).count();

				// 渲染前处理
				commandListDirectPre->ResourceBarrier(
					1,
					&CD3DX12_RESOURCE_BARRIER::Transition(
						renderTargets[currentFrameIndex].Get(),
						D3D12_RESOURCE_STATE_PRESENT,
						D3D12_RESOURCE_STATE_RENDER_TARGET));

				// 偏移描述符指针到指定帧缓冲视图的位置
				CD3DX12_CPU_DESCRIPTOR_HANDLE renderTargetViewDescriptorHandle(
					CPU_DESCRIPTOR_HEAP_START(renderTargetViewDescriptorHeap),
					currentFrameIndex, renderTargetViewDescriptorSize);

				CD3DX12_CPU_DESCRIPTOR_HANDLE depthStencilViewDescriptorHandle(CPU_DESCRIPTOR_HEAP_START(depthStencilViewDescriptorHeap));

				// 设置渲染目标
				commandListDirectPre->OMSetRenderTargets(1, &renderTargetViewDescriptorHandle, FALSE, &depthStencilViewDescriptorHandle);

				commandListDirectPre->RSSetViewports(1, &viewport);
				commandListDirectPre->RSSetScissorRects(1, &scissorRect);

				const float clearColor[]{ 0.4f, 0.6f, 0.9f, 1.0f };

				commandListDirectPre->ClearRenderTargetView(renderTargetViewDescriptorHandle, clearColor, 0, nullptr);
				commandListDirectPre->ClearDepthStencilView(depthStencilViewDescriptorHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0F, 0, 0, nullptr);

				renderImGui();

				calculateFrameStats();

				states = 2;

				waitedHandles.clear();
				waitedHandles.push_back(renderThreadPayloads[SkyboxThreadIndex].eventRenderOver);
				waitedHandles.push_back(renderThreadPayloads[SceneObjectThreadIndex1].eventRenderOver);
				waitedHandles.push_back(renderThreadPayloads[SceneObjectThreadIndex2].eventRenderOver);

				// 通知各子线程开始渲染
				for (int32_t i = 0; i < MaxThreadCount; i++)
				{
					renderThreadPayloads[i].currentFrameIndex = currentFrameIndex;
					renderThreadPayloads[i].startTime = startTime;
					renderThreadPayloads[i].currentTime = currentTime;
					renderThreadPayloads[i].view = camera.GetViewMatrix();
					renderThreadPayloads[i].projection = projection;

					SetEvent(renderThreadPayloads[i].runEvent);
				}
			}
			break;
			// 状态2，表示所有的渲染命令队列都记录完成了，开始后处理和执行命令列表
			case 2:
			{
				commandListDirectPost->ResourceBarrier(
					1,
					&CD3DX12_RESOURCE_BARRIER::Transition(
						renderTargets[currentFrameIndex].Get(),
						D3D12_RESOURCE_STATE_RENDER_TARGET,
						D3D12_RESOURCE_STATE_PRESENT));

				// 关闭命令列表，可以取执行了
				DXCheck(commandListDirectPre->Close(), L"commandListDirectPre close failed!");
				DXCheck(commandListDirectPost->Close(), L"commandListDirectPost close failed!");
				DXCheck(commandListDirectImGui->Close(), L"commandListDirectImGui close failed!");

				commandLists.clear();

				// 执行命令列表(注意命令列表排队组合的方式)
				commandLists.push_back(commandListDirectPre.Get());
				commandLists.push_back(renderThreadPayloads[SkyboxThreadIndex].commandList.Get());
				commandLists.push_back(renderThreadPayloads[SceneObjectThreadIndex1].commandList.Get());
				commandLists.push_back(renderThreadPayloads[SceneObjectThreadIndex2].commandList.Get());
				commandLists.push_back(commandListDirectImGui.Get());
				commandLists.push_back(commandListDirectPost.Get());

				commandQueue->ExecuteCommandLists(static_cast<uint32_t>(commandLists.size()), commandLists.data());

				// 提交画面
				DXCheck(swapChain->Present(1, 0), L"IDXGISwapChain::Present failed!");

				// 开始同步GPU与CPU的执行，先记录围栏标记值
				const UINT64 targetFenceValue = fenceValue;

				DXCheck(commandQueue->Signal(fence.Get(), targetFenceValue), L"ID3D12CommandQueue::Signal failed!");
				fenceValue++;

				// 看命令有没有真正执行到围栏标记的这里，没有就利用事件去等待，注意使用的是命令队列对象的指针
				DXCheck(fence->SetEventOnCompletion(targetFenceValue, fenceEvent), L"ID3D12Fence::SetEventOnCompletion failed!");

				states = 1;

				waitedHandles.clear();
				waitedHandles.push_back(fenceEvent);

				// 更新下帧开始时间为上一帧开始时间
				startTime = currentTime;
			}
			break;
			// 不可能的情况，但为了避免讨厌的编译警告或意外情况保留这个default
			default:
				quit = true;
				break;
			}

			// 处理消息
			while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE | PM_NOYIELD))
			{
				if (msg.message != WM_QUIT)
				{
					TranslateMessage(&msg);
					DispatchMessage(&msg);
				}
				else
				{
					quit = true;
				}
			}
		}
		// 为了能正常处理消息故直接else，因为上面的渲染循环会抢占线程资源
		else
		{

		}

		// 检测一下线程的活动情况，如果有线程已经退出了，就退出循环
		returnValue = WaitForMultipleObjects(static_cast<DWORD>(subTheadHandles.size()), subTheadHandles.data(), FALSE, 0);

		returnValue -= WAIT_OBJECT_0;

		if (returnValue >= 0 && returnValue < MaxThreadCount)
		{
			quit = true;
		}
	}

	//KillTimer(mainWindow, WM_USER + 100);

	// 通知子线程退出
	for (int32_t i = 0; i < MaxThreadCount; i++)
	{
		PostThreadMessage(renderThreadPayloads[i].threadID, WM_QUIT, 0, 0);
	}

	// 等待所有子线程退出
	DWORD dwReturn = WaitForMultipleObjects(static_cast<DWORD>(subTheadHandles.size()), subTheadHandles.data(), FALSE, INFINITE);

	// 清理所有子线程资源
	for (int32_t i = 0; i < MaxThreadCount; i++)
	{
		CloseHandle(renderThreadPayloads[i].threadHandle);
		CloseHandle(renderThreadPayloads[i].eventRenderOver);
	}

	return static_cast<int32_t>(msg.wParam);
}

bool D3DApp::initGLFW()
{
	glfwInit();

	window = glfwCreateWindow(windowWidth, windowHeight, "Render to Texture", nullptr, nullptr);
	mainWindow = glfwGetWin32Window(window);

	if (window == nullptr)
	{
		return false;
	}

	registerGLFWEventCallbacks();

	return true;
}

uint32_t __stdcall D3DApp::renderThread(void* data)
{
	auto* payload = static_cast<RenderThreadPayload*>(data);

	try
	{
		if (payload == nullptr)
		{
			throw DxException(E_INVALIDARG, FUNCTION_NAME, FILE_NAME, __LINE__);
		}

		// DDS
		std::unique_ptr<uint8_t[]> ddsData;
		std::vector<D3D12_SUBRESOURCE_DATA> subResources;
		DDS_ALPHA_MODE alphaMode = DDS_ALPHA_MODE_UNKNOWN;

		ComPtr<ID3D12Resource> texture;

		// 加载DDS纹理
		DXCheck(LoadDDSTextureFromFile(payload->device.Get(),
			payload->ddsFile.c_str(),
			texture.GetAddressOf(),
			ddsData,
			subResources, SIZE_MAX, &alphaMode, &payload->isCubemap),
			L"LoadDDSTextureFromFile failed!");

		uint64_t textureUploadBufferSize = GetRequiredIntermediateSize(texture.Get(), 0,
			static_cast<uint32_t>(subResources.size()));

		D3D12_RESOURCE_DESC textureDesc = texture->GetDesc();

		ComPtr<ID3D12Resource> textureUploadBuffer;

		DXCheck(payload->device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(textureUploadBufferSize),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&textureUploadBuffer)),
			L"Create thread texture upload buffer failed!");

		UpdateSubresources(
			payload->commandList.Get(),
			texture.Get(),
			textureUploadBuffer.Get(),
			0,
			0,
			static_cast<uint32_t>(subResources.size()),
			subResources.data());

		// 同步
		payload->commandList->ResourceBarrier(
			1,
			&CD3DX12_RESOURCE_BARRIER::Transition(
				texture.Get(),
				D3D12_RESOURCE_STATE_COPY_DEST,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

		D3D12_DESCRIPTOR_HEAP_DESC shaderResourceViewDescriptorHeapDesc{};
		shaderResourceViewDescriptorHeapDesc.NumDescriptors = 2; // 1 SRC + 1 CBV
		shaderResourceViewDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		shaderResourceViewDescriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

		ComPtr<ID3D12DescriptorHeap> shaderResourceViewDescriptorHeap;

		DXCheck(payload->device->CreateDescriptorHeap(
			&shaderResourceViewDescriptorHeapDesc,
			IID_PPV_ARGS(&shaderResourceViewDescriptorHeap)),
			L"Create thread shaderResourceViewDescriptorHeap failed!");

		// 创建SRV
		D3D12_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc{};
		shaderResourceViewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		shaderResourceViewDesc.Format = textureDesc.Format;
		if (payload->isCubemap)
		{
			shaderResourceViewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
			shaderResourceViewDesc.TextureCube.MipLevels = 1;
		}
		else
		{
			shaderResourceViewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			shaderResourceViewDesc.Texture2D.MipLevels = 1;
		}

		CD3DX12_CPU_DESCRIPTOR_HANDLE shaderResourceViewDescriptorHandle(CPU_DESCRIPTOR_HEAP_START(shaderResourceViewDescriptorHeap));

		payload->device->CreateShaderResourceView(texture.Get(), &shaderResourceViewDesc, shaderResourceViewDescriptorHandle);

		// 创建Sampler
		D3D12_DESCRIPTOR_HEAP_DESC samplerDescriptorHeapDesc{};
		samplerDescriptorHeapDesc.NumDescriptors = 1;
		samplerDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
		samplerDescriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

		ComPtr<ID3D12DescriptorHeap> samperDescriptorHeap;

		DXCheck(payload->device->CreateDescriptorHeap(&samplerDescriptorHeapDesc,
			IID_PPV_ARGS(&samperDescriptorHeap)),
			L"Create thread samplerDescriptorHeap failed!");

		D3D12_SAMPLER_DESC samplerDesc{};
		samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.MinLOD = 0;
		samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
		samplerDesc.MipLODBias = 0.0f;
		samplerDesc.MaxAnisotropy = 1;
		samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;

		payload->device->CreateSampler(&samplerDesc, CPU_DESCRIPTOR_HEAP_START(samperDescriptorHeap));

		D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
		D3D12_INDEX_BUFFER_VIEW indexBufferView{};

		uint32_t vertexCount = 0;
		uint32_t indexCount = 0;

		DXModel model;

		model.load(payload->objFile);

		vertexCount = static_cast<uint32_t>(model.mesh.vertices.size());
		indexCount = static_cast<uint32_t>(model.mesh.indices.size());

		auto vertexBufferSize = model.mesh.vertexBufferSize;
		auto indexBufferSize = model.mesh.indexBufferSize;

		// 创建Vertex Buffer, 仅使用Upload隐式堆
		ComPtr<ID3D12Resource> vertexBuffer;

		DXCheck(payload->device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&vertexBuffer)), L"Thread create vertex buffer failed!");

		// 使用map-memcpy-unmap 大法将数据传至顶点缓冲对象
		uint8_t* vertexDataBegin = nullptr;
		CD3DX12_RANGE readRange(0, 0);

		DXCheck(vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&vertexDataBegin)), L"Thread vertex buffer map failed!");

		memcpy_s(vertexDataBegin, vertexBufferSize, model.mesh.vertices.data(), vertexBufferSize);

		vertexBuffer->Unmap(0, nullptr);

		// 创建Index Buffer, 仅使用Upload隐式堆
		ComPtr<ID3D12Resource> indexBuffer;

		DXCheck(payload->device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(indexBufferSize),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&indexBuffer)), L"Create thread index buffer failed!");

		uint8_t* indexDataBegin = nullptr;

		DXCheck(indexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&indexDataBegin)), L"Thread index buffer map failed!");

		memcpy_s(indexDataBegin, indexBufferSize, model.mesh.indices.data(), indexBufferSize);

		indexBuffer->Unmap(0, nullptr);

		// 创建Vertex Buffer View
		vertexBufferView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
		vertexBufferView.StrideInBytes = sizeof(DXVertex);
		vertexBufferView.SizeInBytes = vertexBufferSize;

		// 创建Index Buffer View
		indexBufferView.BufferLocation = indexBuffer->GetGPUVirtualAddress();
		indexBufferView.Format = DXGI_FORMAT_R32_UINT;
		indexBufferView.SizeInBytes = indexBufferSize;

		// 创建常量缓冲
		auto modelViewProjectionBufferSize = ROUND_UP(sizeof(ModelViewProjectionBuffer), 256);

		ComPtr<ID3D12Resource> constantBuffer;

		DXCheck(payload->device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(modelViewProjectionBufferSize),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&constantBuffer)
		), L"Create thread constant buffer failed!");

		ModelViewProjectionBuffer* modelViewProjectionBuffer = nullptr;

		// Map之后就不再Unmap了，直接复制数据进去，这样每帧都不用 map-copy-unmap 浪费时间了
		DXCheck(constantBuffer->Map(0, nullptr, reinterpret_cast<void**>(&modelViewProjectionBuffer)),
			L"Thread constantBuffer map failed!");

		// 创建CBV
		D3D12_CONSTANT_BUFFER_VIEW_DESC constantBufferViewDesc{};
		constantBufferViewDesc.BufferLocation = constantBuffer->GetGPUVirtualAddress();
		constantBufferViewDesc.SizeInBytes = modelViewProjectionBufferSize;

		CD3DX12_CPU_DESCRIPTOR_HANDLE constantBufferDescriptorHanle(
			CPU_DESCRIPTOR_HEAP_START(shaderResourceViewDescriptorHeap),
			1, payload->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));

		payload->device->CreateConstantBufferView(&constantBufferViewDesc, constantBufferDescriptorHanle);

		// 设置事件对象，通知并切回主线程，完成资源的第二个copy命令
		DXCheck(payload->commandList->Close(), L"Thread commandlist close failed!");

		// 设置信号，第一次通知主线程本线程资源加载完毕
		SetEvent(payload->eventRenderOver);

		DWORD returnValue = 0;
		bool quit = false;

		MSG msg{};

		// 渲染循环
		while (!quit)
		{
			// 等待主线程通知开始渲染，同时仅接收主线程Post过来的消息，目前就是为了等待WM_QUIT消息
			//returnValue = MsgWaitForMultipleObjects(1, &payload->runEvent, FALSE, INFINITE, QS_ALLPOSTMESSAGE);
			returnValue = WaitForMultipleObjects(1, &payload->runEvent, FALSE, INFINITE);

			switch (returnValue - WAIT_OBJECT_0)
			{
			case 0:
			{
				// 命令分配器先Reset一下，刚才已经执行过了一个复制纹理的命令
				DXCheck(payload->commandAllocator->Reset(), L"Thread commandAllocator reset failed!");

				// Reset命令列表，并重新指定命令分配器和PSO对象
				DXCheck(payload->commandList->Reset(payload->commandAllocator.Get(),
												  payload->pipelineState.Get()),
													L"Thread commandList reset failed!");

				// 准备MVP矩阵，启发式的向大家说明利用多线程渲染时，物理变换可以在不同的线程中，并且在不同的CPU上并行的执行
				static float angle = 0.0f;

				//angle += 0.1f;

				auto model = glm::rotate(glm::mat4(1.0f), glm::radians(angle), glm::vec3(0.0f, 1.0f, 0.0f));
				model = glm::scale(model, payload->scale);
				model = glm::translate(model, payload->position);

				auto view = payload->view;

				if (payload->isCubemap)
				{
					view = glm::mat4(glm::mat3(view));
				}

				auto projection = payload->projection;

				auto modelViewProjection = projection * view * model;

				modelViewProjectionBuffer->model = model;
				modelViewProjectionBuffer->modelViewProjection = modelViewProjection;

				// 设置对应的渲染目标和视裁剪框(这是渲染子线程必须要做的步骤，基本也就是所谓多线程渲染的核心秘密所在了)
				CD3DX12_CPU_DESCRIPTOR_HANDLE renderTargetViewDescriptorHandle(
					CPU_DESCRIPTOR_HEAP_START(payload->renderTargetViewDescriptorHeap),
					payload->currentFrameIndex,
					payload->renderTargetViewDescriptorSize);

				CD3DX12_CPU_DESCRIPTOR_HANDLE depthStencilViewDescriptorHandle(
					CPU_DESCRIPTOR_HEAP_START(payload->depthStencilViewDescriptorHeap));

				// 设置渲染目标
				payload->commandList->OMSetRenderTargets(1,
					&renderTargetViewDescriptorHandle,
					FALSE,
					&depthStencilViewDescriptorHandle);

				payload->commandList->RSSetViewports(1, &payload->viewport);
				payload->commandList->RSSetScissorRects(1, &payload->scissorRect);

				// 渲染(实质就是记录渲染命令列表)
				payload->commandList->SetGraphicsRootSignature(payload->rootSignature.Get());
				payload->commandList->SetPipelineState(payload->pipelineState.Get());

				ID3D12DescriptorHeap* descriptorHeaps[]{ shaderResourceViewDescriptorHeap.Get(), samperDescriptorHeap.Get() };
				payload->commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

				// 设置SRV
				CD3DX12_GPU_DESCRIPTOR_HANDLE gpuShaderResourceViewDescriptorHandle(
					GPU_DESCRIPTOR_HEAP_START(shaderResourceViewDescriptorHeap));

				payload->commandList->SetGraphicsRootDescriptorTable(0, gpuShaderResourceViewDescriptorHandle);

				// 设置CBV
				CD3DX12_GPU_DESCRIPTOR_HANDLE gpuConstantBufferViewDescriptorHandle(
					GPU_DESCRIPTOR_HEAP_START(shaderResourceViewDescriptorHeap),
					1,
					payload->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));

				payload->commandList->SetGraphicsRootDescriptorTable(1, gpuConstantBufferViewDescriptorHandle);

				// 设置Sampler
				payload->commandList->SetGraphicsRootDescriptorTable(2, GPU_DESCRIPTOR_HEAP_START(samperDescriptorHeap));

				// 注意我们使用的渲染手法是三角形列表，也就是通常的Mesh网格
				payload->commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				payload->commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
				payload->commandList->IASetIndexBuffer(&indexBufferView);

				// Draw call!!!
				payload->commandList->DrawIndexedInstanced(indexCount, 1, 0, 0, 0);

				// 完成渲染(即关闭命令列表，并设置同步对象通知主线程开始执行)
				DXCheck(payload->commandList->Close(), L"Thread commandlist close failed!");

				// 设置信号，通知主线程本线程渲染完毕
				SetEvent(payload->eventRenderOver);
			}
			break;
			case 1:
			{
				// 处理消息
				while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
				{
					// 这里只可能是别的线程发过来的消息，用于更复杂的场景
					if (WM_QUIT != msg.message)
					{
						TranslateMessage(&msg);
						DispatchMessage(&msg);
					}
					else
					{
						quit = true;
					}
				}
			}

			break;
			case WAIT_TIMEOUT:
			break;
			default:
				break;
			}
		}
	}
	catch (DxException& exception)
	{
		MessageBox(nullptr, exception.ToString().c_str(), L"HR Failed", MB_OK);
	}

	return 0;
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
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
	swapChainDesc.BufferCount = FrameBackbufferCount;
	swapChainDesc.Width = windowWidth;
	swapChainDesc.Height = windowHeight;
	swapChainDesc.Format = swapChainFormat;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	//swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
	//swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
	swapChainDesc.SampleDesc.Count = 1;
	//swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

	//DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullScreenSwapChainDesc = {};
	//fullScreenSwapChainDesc.Windowed = TRUE;

	DXCheck(DXGIFactory->CreateSwapChainForHwnd(commandQueue.Get(),
		mainWindow,
		&swapChainDesc,
		//&fullScreenSwapChainDesc,
		nullptr,
		nullptr, &swapChain1),
		L"DXGIFactory::CreateSwapChainForHwnd failed!");

	DXCheck(swapChain1.As(&swapChain), L"IDXGISwapChain::As(&swapChain) failed");

	// 得到当前后缓冲区的序号，也就是下一个将要呈送显示的缓冲区的序号
	// 注意此处使用了高版本的SwapChain接口的函数
	currentFrameIndex = swapChain->GetCurrentBackBufferIndex();

	//swapChainDesc.Width = imGuiWindowWidth;
	//swapChainDesc.Height = imGuiWindowHeight;

	//DXCheck(DXGIFactory->CreateSwapChainForHwnd(commandQueue.Get(),
	//	mainWindow,
	//	&swapChainDesc,
	//	//&fullScreenSwapChainDesc,
	//	nullptr,
	//	nullptr, &swapChain1),
	//	L"DXGIFactory::CreateSwapChainForHwnd failed!");

	//DXCheck(swapChain1.As(&imGuiSwapChain), L"IDXGISwapChain::As(&swapChain) failed");

	//imGuiCurrentFrameIndex = imGuiSwapChain->GetCurrentBackBufferIndex();
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

	shaderResourceViewDescriptorHeapDesc.NumDescriptors = 3; // 1 SRV + 1 CBV + 1 DSV

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

void D3DApp::createShaderResourceView(D3D12_SRV_DIMENSION dimension, const ComPtr<ID3D12Resource>& texture, const ComPtr<ID3D12DescriptorHeap>& descriptorHeap, uint32_t index)
{
	D3D12_RESOURCE_DESC textureDesc = texture->GetDesc();

	D3D12_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc{};
	shaderResourceViewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	shaderResourceViewDesc.ViewDimension = dimension;

	shaderResourceViewDesc.Format = textureDesc.Format;
	if (dimension == D3D12_SRV_DIMENSION_TEXTURECUBE)
	{
		shaderResourceViewDesc.TextureCube.MipLevels = textureDesc.MipLevels;
	}
	else
	{
		shaderResourceViewDesc.Texture2D.MipLevels = textureDesc.MipLevels;
	}

	CD3DX12_CPU_DESCRIPTOR_HANDLE descriptorHandle(
		CPU_DESCRIPTOR_HEAP_START(descriptorHeap),
		index,
		SRVCBVUAVDescriptorSize);

	device->CreateShaderResourceView(texture.Get(), &shaderResourceViewDesc, descriptorHandle);
}

std::shared_ptr<MeshGeometry> D3DApp::createMeshGeometry(const DXModel& model)
{
	auto localMeshGeometry = std::make_shared<MeshGeometry>();
	localMeshGeometry->Name = model.name;

	auto vertexBufferSize = model.mesh.vertexBufferSize;
	DXCheck(D3DCreateBlob(vertexBufferSize, &localMeshGeometry->VertexBufferCPU), L"D3DCreateBlob failed!");
	CopyMemory(localMeshGeometry->VertexBufferCPU->GetBufferPointer(), model.mesh.vertices.data(), vertexBufferSize);

	auto indexBufferSize = model.mesh.indexBufferSize;
	DXCheck(D3DCreateBlob(indexBufferSize, &localMeshGeometry->IndexBufferCPU), L"D3DCreateBlob failed!");
	CopyMemory(localMeshGeometry->IndexBufferCPU->GetBufferPointer(), model.mesh.vertices.data(), indexBufferSize);

	localMeshGeometry->VertexBufferGPU = Utils::createDefaultBuffer(device.Get(), 
																	commandListDirectPre.Get(),
															model.mesh.vertices.data(), 
															vertexBufferSize, 
														localMeshGeometry->VertexBufferUploader);

	localMeshGeometry->IndexBufferGPU = Utils::createDefaultBuffer(device.Get(),
																   commandListDirectPre.Get(),
														   model.mesh.indices.data(),
														   indexBufferSize,
													   localMeshGeometry->IndexBufferUploader);
	
	localMeshGeometry->VertexByteStride = sizeof(DXVertex);
	localMeshGeometry->VertexBufferByteSize = model.mesh.vertexBufferSize;
	localMeshGeometry->IndexFormat = DXGI_FORMAT_R32_UINT;
	localMeshGeometry->IndexBufferByteSize = model.mesh.indexBufferSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = static_cast<uint32_t>(model.mesh.indices.size());
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	localMeshGeometry->DrawArgs[model.name] = submesh;

	return localMeshGeometry;
}

void D3DApp::createSkyboxDescriptorHeap()
{
	D3D12_DESCRIPTOR_HEAP_DESC skyboxDescriptorHeapDesc{};
	skyboxDescriptorHeapDesc.NumDescriptors = 2; // 1 SRV + 1 CBV
	skyboxDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	skyboxDescriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	DXCheck(device->CreateDescriptorHeap(&skyboxDescriptorHeapDesc, IID_PPV_ARGS(&skyboxDescriptorHeap)), L"Create skybox descriptor heap failed!");

	createDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
						2,
						true,
						skyboxDescriptorHeap, ObjectName(skyboxDescriptorHeap));
}

void D3DApp::createSkyboxDescriptors()
{
	createShaderResourceView(D3D12_SRV_DIMENSION_TEXTURECUBE, skyboxTexture, skyboxDescriptorHeap);
}

void D3DApp::createRenderTargetView()
{
	CD3DX12_CPU_DESCRIPTOR_HANDLE renderTargetViewDescriptorHandle(CPU_DESCRIPTOR_HEAP_START(renderTargetViewDescriptorHeap));

	for (auto i = 0; i < FrameBackbufferCount; i++)
	{
		DXCheck(swapChain->GetBuffer(i, IID_PPV_ARGS(&renderTargets[i])), L"IDXGISwapChain::GetBuffer failed!");
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

void D3DApp::createGraphicsPipelineState()
{
	ComPtr<IDxcBlob> vertexShader;
	ComPtr<IDxcBlob> pixelShader;

	if (compileOnTheFly)
	{
		Utils::compileShader(L"Assets/Shaders/7.RenderToTexture.hlsl", L"VSMain", L"vs_6_0", vertexShader);
		Utils::compileShader(L"Assets/Shaders/7.RenderToTexture.hlsl", L"PSMain", L"ps_6_0", pixelShader);
	}
	else
	{
		vertexShader = reinterpret_cast<IDxcBlob*>(Utils::loadShaderBinary("Assets/Shaders/7.RenderToTextureVS.dxil"));
		pixelShader = reinterpret_cast<IDxcBlob*>(Utils::loadShaderBinary("Assets/Shaders/7.RenderToTexturePS.dxil"));
	}

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
	graphicsPipelineStateDesc.VS.BytecodeLength = vertexShader->GetBufferSize();
	graphicsPipelineStateDesc.VS.pShaderBytecode = vertexShader->GetBufferPointer();
	graphicsPipelineStateDesc.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };

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
	graphicsPipelineStateDesc.DSVFormat = depthStencilFormat;
	graphicsPipelineStateDesc.SampleMask = UINT_MAX;
	graphicsPipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	graphicsPipelineStateDesc.NumRenderTargets = 1;
	graphicsPipelineStateDesc.RTVFormats[0] = swapChainFormat;
	graphicsPipelineStateDesc.SampleDesc.Count = 1;

	DXCheck(device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc,
		IID_PPV_ARGS(&graphicsPipelineState)),
		L"ID3D12Device::CreateGraphicsPipelineState failed!");

	graphicsPipelineStateDesc.pRootSignature = renderTextureRootSignature.Get();

	DXCheck(device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc,
		IID_PPV_ARGS(&renderTextureGraphicsPipelineState)),
		L"ID3D12Device::CreateGraphicsPipelineState failed!");
}

void D3DApp::createSkyboxGraphicsPipelineState()
{
	TCHAR* shaderFileName = L"Assets/Shaders/7.Skybox.hlsl";

	ComPtr<IDxcBlob> skyboxVertexShader;
	ComPtr<IDxcBlob> skyboxPixelShader;

	if (compileOnTheFly)
	{
		Utils::compileShader(shaderFileName, L"VSMain", L"vs_6_0", skyboxVertexShader);
		Utils::compileShader(shaderFileName, L"PSMain", L"ps_6_0", skyboxPixelShader);
	}
	else
	{
		skyboxVertexShader = reinterpret_cast<IDxcBlob*>(Utils::loadShaderBinary("Assets/Shaders/7.SkyboxVS.dxil"));
		skyboxPixelShader = reinterpret_cast<IDxcBlob*>(Utils::loadShaderBinary("Assets/Shaders/7.SkyboxPS.dxil"));
	}
	// 天空盒子只有顶点，只有位置参数
	D3D12_INPUT_ELEMENT_DESC skyboxInputElementDesc[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	// 创建天空盒的PSO, 注意天空盒子不需要深度测试
	D3D12_GRAPHICS_PIPELINE_STATE_DESC skyboxGraphicsPipelineStateDesc{};
	skyboxGraphicsPipelineStateDesc.DepthStencilState.DepthEnable = FALSE;
	skyboxGraphicsPipelineStateDesc.DepthStencilState.StencilEnable = FALSE;
	skyboxGraphicsPipelineStateDesc.InputLayout = { skyboxInputElementDesc, _countof(skyboxInputElementDesc) };
	skyboxGraphicsPipelineStateDesc.pRootSignature = rootSignature.Get();
	//skyboxGraphicsPipelineStateDesc.VS = { skyboxVertexShader->GetBufferPointer(), skyboxVertexShader->GetBufferSize() };
	//skyboxGraphicsPipelineStateDesc.PS = { skyboxPixelShader->GetBufferPointer(), skyboxPixelShader->GetBufferSize() };
	skyboxGraphicsPipelineStateDesc.VS.BytecodeLength = skyboxVertexShader->GetBufferSize();
	skyboxGraphicsPipelineStateDesc.VS.pShaderBytecode = skyboxVertexShader->GetBufferPointer();
	skyboxGraphicsPipelineStateDesc.PS.BytecodeLength = skyboxPixelShader->GetBufferSize();
	skyboxGraphicsPipelineStateDesc.PS.pShaderBytecode = skyboxPixelShader->GetBufferPointer();

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

	createCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocatorDirectPre, graphicsPipelineState, commandListDirectPre);

	createCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocatorDirectPost);

	createCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocatorDirectPost, graphicsPipelineState, commandListDirectPost);

	createCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocatorDirectImGui);

	createCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocatorDirectImGui, graphicsPipelineState, commandListDirectImGui);

	createCommandAllocator(D3D12_COMMAND_LIST_TYPE_BUNDLE, commandAllocatorSkybox);

	createCommandList(D3D12_COMMAND_LIST_TYPE_BUNDLE, commandAllocatorSkybox, skyboxGraphicsPipelineState, skyboxBundle);

	createCommandAllocator(D3D12_COMMAND_LIST_TYPE_BUNDLE, commandAllocatorScene);

	createCommandList(D3D12_COMMAND_LIST_TYPE_BUNDLE, commandAllocatorScene, graphicsPipelineState, sceneBundle);

	createCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocatorRenderTextureObject);

	createCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocatorRenderTextureObject, graphicsPipelineState, renderTextureObject);
}

void D3DApp::createFence()
{
	DXCheck(device->CreateFence(0,
		D3D12_FENCE_FLAG_NONE,
		IID_PPV_ARGS(&fence)),
		L"ID3D12Device::CreateFence failed!");

	fenceEvent = CreateEventEx(nullptr, FALSE, FALSE, EVENT_ALL_ACCESS);

	// Fence初始值要大于0，否则后面的if (fence->GetCompletedValue() < currentFenceValue)永远为false
	fenceValue = 1;
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
		IID_PPV_ARGS(&texture)), L"ID3D12Device::CreatePlacedResource failed!");

	// 获取上传堆资源缓冲的大小，这个尺寸通常大于实际图片的尺寸
	textureUploadBufferSize = GetRequiredIntermediateSize(texture.Get(), 0, 1);

	// 使用“定位方式”创建用于上传纹理数据的缓冲资源

	// 创建用于上传纹理的资源, 注意其类型是Buffer
	// 上传堆对于GPU访问来说性能是很差的，
	// 所以对于几乎不变的数据尤其像纹理都是
	// 通过它来上传至GPU访问更高效的默认堆中
	ComPtr<ID3D12Resource> textureUploadBuffer;

	DXCheck(device->CreatePlacedResource(uploadHeap.Get(),
		0,
		&CD3DX12_RESOURCE_DESC::Buffer(textureUploadBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&textureUploadBuffer)), L"ID3D12Device::CreatePlacedResource failed!");

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

	DXCheck(textureUploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&data)), L"ID3D12Resource::Map failed!");

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
	textureUploadBuffer->Unmap(0, nullptr);

	// 释放图片数据，做一个干净的程序员
	HeapFree(GetProcessHeap(), 0, textureData);

	loadSkyboxTexture();

	// 向直接命令列表发出从上传堆复制纹理数据到默认堆的命令，
	// 执行同步并等待，即完成第二个copy动作，由GPU上的复制引擎完成
	// 注意此时直接命令列表还没有绑定PSO对象，因此它也是不能执行3D图形命令的
	// 但是可以执行复制命令，因为复制引擎不需要什么额外的状态设置之类的参数
	CD3DX12_TEXTURE_COPY_LOCATION dest(texture.Get(), 0);
	CD3DX12_TEXTURE_COPY_LOCATION src(textureUploadBuffer.Get(), textureLayout);

	commandListDirectPre->CopyTextureRegion(&dest, 0, 0, 0, &src, nullptr);

	// 设置一个资源屏障，同步并确认复制操作完成
	// 直接使用结构体然后调用的形式
	D3D12_RESOURCE_BARRIER resourceBarrier{};
	resourceBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	resourceBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	resourceBarrier.Transition.pResource = texture.Get();
	resourceBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	resourceBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	resourceBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

	commandListDirectPre->ResourceBarrier(1, &resourceBarrier);

	// 或者使用D3DX12库中的工具类调用的等价形式，下面的方式更简洁一些
	// pIcommandListDirect->ResourceBarrier(1
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
	device->CreateShaderResourceView(texture.Get(), &shaderResourceViewDesc, CPU_DESCRIPTOR_HEAP_START(shaderResourceViewDescriptorHeap));

	// 执行命令列表并等待纹理资源上传完成，这一步是必须的
	DXCheck(commandListDirectPre->Close(), L"ID3D12GraphicsCommandList::Close failed!");
	ID3D12CommandList* commandLists[] = { commandListDirectPre.Get() };
	commandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);

	flushCommandQueue();

	//命令分配器先Reset一下，刚才已经执行过了一个复制纹理的命令
	DXCheck(commandAllocatorDirectPre->Reset(), L"ID3D12CommandAllocator::Reset failed!");

	//Reset命令列表，并重新指定命令分配器和PSO对象
	DXCheck(commandListDirectPre->Reset(commandAllocatorDirectPre.Get(), graphicsPipelineState.Get()), L"ID3D12GraphicsCommandList::Reset failed!");

	loadCubeResource();

	//bunny.load("Assets/Models/bunny.obj");
	bunny.load("Assets/Models/cube.obj", "Cube");
	skybox.load("Assets/Models/cube.obj", "Cube");

	vertexCount = static_cast<uint32_t>(bunny.mesh.vertices.size());
	indexCount = static_cast<uint32_t>(bunny.mesh.indices.size());
	triangleCount = indexCount / 3;
	vertexBufferSize = bunny.mesh.vertexBufferSize;

	indexCount = static_cast<uint32_t>(bunny.mesh.indices.size());
	indexBufferSize = bunny.mesh.indexBufferSize;

	skyboxIndexCount = static_cast<uint32_t>(skybox.mesh.indices.size());

	model = createMeshGeometry(bunny);
}

void D3DApp::flushCommandQueue()
{
	const uint64_t currentFenceValue = fenceValue;

	// Add an instruction to the command queue to set a new fence point.  Because we 
	// are on the GPU timeline, the new fence point won't be set until the GPU finishes
	// processing all the commands prior to this Signal().
	DXCheck(commandQueue->Signal(fence.Get(), currentFenceValue), L"ID3D12CommandQueue::Signal failed!");
	fenceValue++;

	// 看命令有没有真正执行到围栏标记的这里，没有就利用事件去等待，注意使用的是命令队列对象的指针
	// Wait until the GPU has completed commands up to this fence point.
	if (fence->GetCompletedValue() < currentFenceValue)
	{
		auto fenceEvent = CreateEventEx(nullptr, FALSE, FALSE, EVENT_ALL_ACCESS);

		if (fenceEvent == nullptr)
		{
			DXCheck(HRESULT_FROM_WIN32(GetLastError()), L"CreateEvent failed!");
		}

		// Fire event when GPU hits current fence.  
		DXCheck(fence->SetEventOnCompletion(currentFenceValue, fenceEvent), L"ID3D12Fence::SetEventOnCompletion failed!");
		WaitForSingleObject(fenceEvent, INFINITE);
		CloseHandle(fenceEvent);
	}
}

void D3DApp::loadSkyboxTexture()
{
	//加载Skybox的 Cube Map 需要的变量
	std::unique_ptr<uint8_t[]> ddsData;
	std::vector<D3D12_SUBRESOURCE_DATA> subResources;
	DDS_ALPHA_MODE alphaMode = DDS_ALPHA_MODE_UNKNOWN;
	bool bIsCube = false;

	TCHAR* skyboxTextureFile = L"Assets/Textures/Day_1024.dds";

	ID3D12Resource* skyboxTextureResource = nullptr;

	DXCheck(LoadDDSTextureFromFile(
		device.Get(),
		skyboxTextureFile,
		&skyboxTextureResource,
		ddsData,
		subResources,
		SIZE_MAX,
		&alphaMode,
		&bIsCube
		), L"LoadDDSTextureFromFile failed!");

	// 上面函数加载的纹理在隐式默认堆上，两个Copy动作需要我们自己完成
	skyboxTexture.Attach(skyboxTextureResource);

	// 获取skybox的资源大小，并创建上传堆
	skyboxTextureUploadBufferSize = GetRequiredIntermediateSize(skyboxTexture.Get(), 0, static_cast<uint32_t>(subResources.size()));

	D3D12_HEAP_DESC skyboxUploadHeapDesc{};
	skyboxUploadHeapDesc.Alignment = 0;
	skyboxUploadHeapDesc.SizeInBytes = ROUND_UP(2 * skyboxTextureUploadBufferSize, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
	skyboxUploadHeapDesc.Properties.Type = D3D12_HEAP_TYPE_UPLOAD;
	skyboxUploadHeapDesc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	skyboxUploadHeapDesc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

	// 上传堆就是缓冲，可以摆放任意数据
	skyboxUploadHeapDesc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;

	DXCheck(device->CreateHeap(&skyboxUploadHeapDesc, IID_PPV_ARGS(&skyboxUploadHeap)), L"Create skybox upload heap failed!");

	setD3D12DebugNameComPtr(skyboxUploadHeap);

	// 在skybox上传堆中创建用于上传纹理数据的缓冲资源
	DXCheck(device->CreatePlacedResource(
		skyboxUploadHeap.Get(),
		0,
		&CD3DX12_RESOURCE_DESC::Buffer(skyboxTextureUploadBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&skyboxTextureUploadBuffer)), L"Create skybox texture upload buffer failed!");

	// 上传skybox的纹理
	UpdateSubresources(
		commandListDirectPre.Get(),
		skyboxTexture.Get(),
		skyboxTextureUploadBuffer.Get(),
		0,
		0,
		static_cast<uint32_t>(subResources.size()),
		subResources.data()
	);

	commandListDirectPre->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			skyboxTexture.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
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
	createShaderResourceView(D3D12_SRV_DIMENSION_TEXTURE2D, renderTexture, imGuiShaderResourceViewDescriptorHeap, 8);
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
									  ModelViewProjectionBuffer** modelViewProjectionBuffer)
{
	// 以“定位方式”创建常量缓冲
	bufferOffset = ROUND_UP(bufferOffset + indexBufferSize, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

	constantBufferSize = ROUND_UP(sizeof(ModelViewProjectionBuffer), 256) * 256;

	// 创建常量缓冲区，注意缓冲区尺寸设置为256边界对齐大小
	DXCheck(device->CreatePlacedResource(
		heap.Get(),
		offset,
		&CD3DX12_RESOURCE_DESC::Buffer(size),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(constantBuffer.GetAddressOf())),
		L"Create constant buffer failed!");

	// Map之后就不再Unmap了，直接复制数据进去，这样每帧都不用 map-copy-unmap 浪费时间了
	DXCheck(constantBuffer->Map(0, nullptr, reinterpret_cast<void**>(modelViewProjectionBuffer)), L"ID3D12Resource::Map failed!");

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

	constantBufferSize = ROUND_UP(sizeof (ModelViewProjectionBuffer), 256) * 256;

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

	createConstantBufferView(uploadHeap, bufferOffset, constantBufferSize, shaderResourceViewDescriptorHeap, 1, constantBuffer, &modelViewProjectionBuffer);
}

void D3DApp::createSkyboxConstantBufferView()
{
	// 以“定位方式”创建常量缓冲
	skyboxBufferOffset = ROUND_UP(skyboxBufferOffset + skyboxIndexBufferSize, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

	skyboxConstantBufferSize = ROUND_UP(sizeof(ModelViewProjectionBuffer), 256) * 256;

	// 创建常量缓冲区，注意缓冲区尺寸设置为256边界对齐大小
	DXCheck(device->CreatePlacedResource(
		skyboxUploadHeap.Get(),
		skyboxBufferOffset,
		&CD3DX12_RESOURCE_DESC::Buffer(skyboxConstantBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&skyboxConstantBuffer)),
		L"Create skybox constant buffer failed!");

	// Map之后就不再Unmap了，直接复制数据进去，这样每帧都不用 map-copy-unmap 浪费时间了
	DXCheck(skyboxConstantBuffer->Map(0, nullptr, reinterpret_cast<void**>(&skyboxModelViewProjectionBuffer)), L"ID3D12Resource::Map failed!");

	D3D12_CONSTANT_BUFFER_VIEW_DESC skyboxConstantBufferViewDesc{};
	skyboxConstantBufferViewDesc.BufferLocation = skyboxConstantBuffer->GetGPUVirtualAddress();
	skyboxConstantBufferViewDesc.SizeInBytes = skyboxConstantBufferSize;

	CD3DX12_CPU_DESCRIPTOR_HANDLE skyboxConstantBufferViewHandle(CPU_DESCRIPTOR_HEAP_START(skyboxDescriptorHeap),
		1,
		SRVCBVUAVDescriptorSize);

	device->CreateConstantBufferView(&skyboxConstantBufferViewDesc, skyboxConstantBufferViewHandle);
}

void D3DApp::createRenderTextureConstantBufferView()
{
	bufferOffset = ROUND_UP(bufferOffset + constantBufferSize, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

	constantBufferSize = ROUND_UP(sizeof(ModelViewProjectionBuffer), 256) * 256;

	//createConstantBufferView(uploadHeap, bufferOffset, constantBufferSize, renderTextureSRVDescriptorHeap, 1, renderTextureConstantBuffer, &renderTextureModelViewProjectionBuffer);
	createConstantBufferView(uploadHeap, bufferOffset, constantBufferSize, imGuiShaderResourceViewDescriptorHeap, 9, renderTextureConstantBuffer, &renderTextureModelViewProjectionBuffer);
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
	sceneBundle->SetPipelineState(graphicsPipelineState.Get());
	sceneBundle->SetGraphicsRootSignature(rootSignature.Get());

	// 设置要使用的描述符堆
	ID3D12DescriptorHeap* descriptorHeaps[] = { shaderResourceViewDescriptorHeap.Get(), samplerDescriptorHeap.Get() };
	sceneBundle->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	sceneBundle->SetGraphicsRootDescriptorTable(0, GPU_DESCRIPTOR_HEAP_START(shaderResourceViewDescriptorHeap));

	CD3DX12_GPU_DESCRIPTOR_HANDLE constantBufferViewHandle(GPU_DESCRIPTOR_HEAP_START(shaderResourceViewDescriptorHeap), 1, SRVCBVUAVDescriptorSize);

	sceneBundle->SetGraphicsRootDescriptorTable(1, constantBufferViewHandle);

	CD3DX12_GPU_DESCRIPTOR_HANDLE samplerDescriptorHandle(GPU_DESCRIPTOR_HEAP_START(samplerDescriptorHeap), currentSamplerNo, samplerDescriptorSize);

	sceneBundle->SetGraphicsRootDescriptorTable(2, samplerDescriptorHandle);

	// 注意我们使用的渲染手法是三角形列表，也就是通常的Mesh网格
	sceneBundle->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	//sceneBundle->IASetVertexBuffers(0, 1, &vertexBufferView);
	//sceneBundle->IASetIndexBuffer(&indexBufferView);

	//sceneBundle->DrawIndexedInstanced(indexCount, 1, 0, 0, 0);

	sceneBundle->IASetVertexBuffers(0, 1, &model->VertexBufferView());
	sceneBundle->IASetIndexBuffer(&model->IndexBufferView());

	sceneBundle->DrawIndexedInstanced(model->DrawArgs["Cube"].IndexCount, 1, 0, 0, 0);

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

	skyboxBundle->SetGraphicsRootDescriptorTable(0, GPU_DESCRIPTOR_HEAP_START(skyboxDescriptorHeap));

	CD3DX12_GPU_DESCRIPTOR_HANDLE skyboxConstantBufferViewHandle(GPU_DESCRIPTOR_HEAP_START(skyboxDescriptorHeap), 1, SRVCBVUAVDescriptorSize);

	skyboxBundle->SetGraphicsRootDescriptorTable(1, skyboxConstantBufferViewHandle);

	CD3DX12_GPU_DESCRIPTOR_HANDLE skyboxSamplerDescriptorHandle(GPU_DESCRIPTOR_HEAP_START(skyboxSamplerDescriptorHeap));

	skyboxBundle->SetGraphicsRootDescriptorTable(2, skyboxSamplerDescriptorHandle);

	// 注意我们使用的渲染手法是三角形列表，也就是通常的Mesh网格
	skyboxBundle->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	skyboxBundle->IASetVertexBuffers(0, 1, &skyboxVertexBufferView);
	skyboxBundle->IASetIndexBuffer(&skyboxIndexBufferView);

	skyboxBundle->DrawIndexedInstanced(skyboxIndexCount, 1, 0, 0, 0);

	skyboxBundle->Close();
}

void D3DApp::prepareRenderThreads()
{
	// 天空盒个性参数
	renderThreadPayloads[SkyboxThreadIndex].ddsFile = L"Assets/Textures/Sunset_1024.dds";
	renderThreadPayloads[SkyboxThreadIndex].objFile = "Assets/Models/cube.obj";
	renderThreadPayloads[SkyboxThreadIndex].position = { 0.0f, 0.0f, 0.0f };
	renderThreadPayloads[SkyboxThreadIndex].scale = { 1.0f, 1.0f, 1.0f };
	renderThreadPayloads[SkyboxThreadIndex].pipelineState = skyboxGraphicsPipelineState;
	renderThreadPayloads[SkyboxThreadIndex].isCubemap = true;
	renderThreadPayloads[SkyboxThreadIndex].view = camera.GetViewMatrix();
	renderThreadPayloads[SkyboxThreadIndex].projection = camera.GetViewMatrix();

	// obj 模型个性参数
	renderThreadPayloads[SceneObjectThreadIndex1].ddsFile = L"Assets/Textures/Noise.dds";
	renderThreadPayloads[SceneObjectThreadIndex1].objFile = "Assets/Models/cube.obj";
	renderThreadPayloads[SceneObjectThreadIndex1].position = { -1.5f, 1.0f, 0.0f };
	renderThreadPayloads[SceneObjectThreadIndex1].scale = { 0.8f, 0.8f, 0.8f };

	renderThreadPayloads[SceneObjectThreadIndex1].pipelineState = graphicsPipelineState;
	renderThreadPayloads[SceneObjectThreadIndex1].isCubemap = false;
	renderThreadPayloads[SceneObjectThreadIndex1].view = camera.GetViewMatrix();

	renderThreadPayloads[SceneObjectThreadIndex2].ddsFile = L"Assets/Textures/Noise.dds";
	renderThreadPayloads[SceneObjectThreadIndex2].objFile = "Assets/Models/cube.obj";
	renderThreadPayloads[SceneObjectThreadIndex2].position = { 1.5f, 0.0f, 0.0f };
	renderThreadPayloads[SceneObjectThreadIndex2].scale = { 1.0f, 1.0f, 1.0f };

	renderThreadPayloads[SceneObjectThreadIndex2].pipelineState = graphicsPipelineState;
	renderThreadPayloads[SceneObjectThreadIndex2].isCubemap = false;
	renderThreadPayloads[SceneObjectThreadIndex2].view = camera.GetViewMatrix();

	// 物体共性参数，也就是各线程的共性参数
	for (int32_t i = 0; i < MaxThreadCount; i++)
	{
		renderThreadPayloads[i].index = i;

		// 创建每个线程需要的命令列表和复制命令队列
		createCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, renderThreadPayloads[i].commandAllocator, L"ThreadCommandAllocator", i);
		createCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, renderThreadPayloads[i].commandAllocator, graphicsPipelineState, renderThreadPayloads[i].commandList, L"ThreadCommandList", i);

		renderThreadPayloads[i].mainThreadID = GetCurrentThreadId();
		renderThreadPayloads[i].mainThreadHandle = GetCurrentThread();
		renderThreadPayloads[i].runEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		renderThreadPayloads[i].eventRenderOver = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		renderThreadPayloads[i].device = device;
		renderThreadPayloads[i].rootSignature = rootSignature;
		renderThreadPayloads[i].renderTargetViewDescriptorHeap = renderTargetViewDescriptorHeap;
		renderThreadPayloads[i].depthStencilViewDescriptorHeap = depthStencilViewDescriptorHeap;
		renderThreadPayloads[i].viewport = viewport;
		renderThreadPayloads[i].scissorRect = scissorRect;
		renderThreadPayloads[i].renderTargetViewDescriptorSize = renderTargetViewDescriptorSize;

		// 添加到被等待队列里
		waitedHandles.push_back(renderThreadPayloads[i].eventRenderOver);

		// 以暂停方式创建线程
		renderThreadPayloads[i].threadHandle = (HANDLE)_beginthreadex(
			nullptr, 0, renderThread,
			static_cast<void*>(&renderThreadPayloads[i]), 
			CREATE_SUSPENDED, 
			&renderThreadPayloads[i].threadID);

		// 然后判断线程是否创建成功
		if (renderThreadPayloads[i].threadHandle == nullptr ||
			renderThreadPayloads[i].threadHandle == reinterpret_cast<HANDLE>(-1))
		{
			throw DxException(HRESULT_FROM_WIN32(GetLastError()), FUNCTION_NAME, FILE_NAME, __LINE__);
		}

		subTheadHandles.push_back(renderThreadPayloads[i].threadHandle);
	}

	// 逐一启动线程
	for (int32_t i = 0; i < MaxThreadCount; i++)
	{
		ResumeThread(renderThreadPayloads[i].threadHandle);
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
	createShaderResourceViewDescriptorHeap();
	//createRenderTargetView();
	createRootSignature();
	Utils::createDXCCompiler();
	createGraphicsPipelineState();
	createSkyboxGraphicsPipelineState();
	createCommandLists();
	createFence();
	
	// 创建NM大小的上传堆，纹理，顶点缓冲和索引缓冲都会用到
	createUploadHeap(ROUND_UP(1024 * 1024 * 100, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT));
	loadResources();
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
	//createDepthStencilView();
	createSamplerDescriptorHeap();
	createSamplers();
	createSkyboxSamplerDescriptorHeap();
	createSkyboxSampler();
	initImGui();
	recordCommands();
	//prepareRenderThreads();
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

		ImGui::SliderFloat("float", &f, 0.0f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f
		ImGui::ColorEdit3("clear color", (float*)&clearColor); // Edit 3 floats representing a color

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
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin("Viewport");

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

void D3DApp::renderImGui()
{
	// 偏移描述符指针到指定帧缓冲视图的位置
	CD3DX12_CPU_DESCRIPTOR_HANDLE renderTargetViewDescriptorHandle(
		CPU_DESCRIPTOR_HEAP_START(renderTargetViewDescriptorHeap),
		currentFrameIndex, renderTargetViewDescriptorSize);

	CD3DX12_CPU_DESCRIPTOR_HANDLE depthStencilViewDescriptorHandle(CPU_DESCRIPTOR_HEAP_START(depthStencilViewDescriptorHeap));

	// 使用单独的命令列表渲染ImGui
	commandListDirectImGui->OMSetRenderTargets(1, &renderTargetViewDescriptorHandle, FALSE, &depthStencilViewDescriptorHandle);

	commandListDirectImGui->RSSetViewports(1, &viewport);
	commandListDirectImGui->RSSetScissorRects(1, &scissorRect);

	ID3D12DescriptorHeap* imGuiDescriptorHeaps[] = { imGuiShaderResourceViewDescriptorHeap.Get() };
	commandListDirectImGui->SetDescriptorHeaps(1, imGuiDescriptorHeaps);

	updateImGui();
	onGUIRender();

	renderImGui(commandListDirectImGui);
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
	modelRotationYAngle += static_cast<float>((currentTime - startTime) * angularVelocity);

	startTime = currentTime;

	// 旋转角度是2PI周期的倍数，去掉周期数，只留下相对0弧度开始的小于2PI的弧度即可
	if (modelRotationYAngle > XM_2PI)
	{
		modelRotationYAngle = std::fmodf(modelRotationYAngle, XM_2PI);
	}

	auto model = glm::mat4(1.0f);
	auto view = camera.GetViewMatrix();
	projection = glm::perspective(glm::radians(camera.Zoom), aspectRatio, 0.1f, 100.0f);

	auto modelViewProjection = projection * view * model;

	modelViewProjectionBuffer->model = model;
	modelViewProjectionBuffer->modelViewProjection = modelViewProjection;

	renderTextureModelViewProjectionBuffer->model = model;
	renderTextureModelViewProjectionBuffer->modelViewProjection = modelViewProjection;

	// 模型矩阵model
	//XMMATRIX model = XMMatrixRotationY(modelRotationYAngle);

	//XMMATRIX view = XMMatrixLookAtLH(XMLoadFloat3(&eye), XMLoadFloat3(&center), XMLoadFloat3(&up));

	//XMMATRIX projection = XMMatrixPerspectiveFovLH(XM_PIDIV4, static_cast<float>(windowWidth) / static_cast<float>(windowHeight), 0.1f, 1000.0f);
	//
	//// 计算model * view * projection矩阵
	//XMMATRIX modelViewProjection = model * view * projection;

	//model = XMMatrixTranspose(model);

	//modelViewProjection = XMMatrixTranspose(modelViewProjection);

	//XMStoreFloat4x4(&modelViewProjectionBuffer->model, model);

	//XMStoreFloat4x4(&modelViewProjectionBuffer->modelViewProjection, modelViewProjection);
}

void D3DApp::updateSkyboxConstantBuffer()
{
	static float angle = 0.0f;

	auto model = glm::rotate(glm::mat4(1.0f), glm::radians(angle += FrameTime), glm::vec3(0.0f, 1.0f, 0.0f));;
	//auto view = camera.GetViewMatrix();
	auto view = glm::mat4(glm::mat3(camera.GetViewMatrix()));
	auto projection = glm::perspective(glm::radians(45.0f), aspectRatio, 0.1f, 100.0f);

	auto modelViewProjection = projection * view * model;

	skyboxModelViewProjectionBuffer->model = model;
	skyboxModelViewProjectionBuffer->modelViewProjection = modelViewProjection;

	//// 模型矩阵model
	//XMMATRIX model = XMMatrixScaling(2.0f, 2.0f, 2.0f);

	//XMMATRIX view = XMMatrixLookAtLH(XMLoadFloat3(&eye), XMLoadFloat3(&center), XMLoadFloat3(&up));

	//// 消除平移部分
	//view.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);

	//XMMATRIX projection = XMMatrixPerspectiveFovLH(XM_PIDIV4, static_cast<float>(windowWidth) / static_cast<float>(windowHeight), 0.1f, 1000.0f);

	//// 计算model * view * projection矩阵
	//XMMATRIX modelViewProjection = model * view * projection;

	//model = XMMatrixTranspose(model);

	//modelViewProjection = XMMatrixTranspose(modelViewProjection);

	//XMStoreFloat4x4(&skyboxModelViewProjectionBuffer->model, model);

	//XMStoreFloat4x4(&skyboxModelViewProjectionBuffer->modelViewProjection, modelViewProjection);
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

void D3DApp::update()
{
	updateConstantBuffer();
	updateSkyboxConstantBuffer();
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
}

void D3DApp::render()
{
	//设置屏障将渲染目标纹理从资源转换为渲染目标状态
	commandListDirectPre->ResourceBarrier(1
		, &CD3DX12_RESOURCE_BARRIER::Transition(
			renderTexture.Get()
			, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
			, D3D12_RESOURCE_STATE_RENDER_TARGET)
	);

	// 第一遍，渲染到纹理
	CD3DX12_CPU_DESCRIPTOR_HANDLE renderTextureRTVDescriptorHandle(CPU_DESCRIPTOR_HEAP_START(renderTextureRTVDescriptorHeap));
	CD3DX12_CPU_DESCRIPTOR_HANDLE renderTextureDSVDecriptorHandle(CPU_DESCRIPTOR_HEAP_START(renderTextureDSVDescriptorHeap));
	
	commandListDirectPre->OMSetRenderTargets(1, &renderTextureRTVDescriptorHandle, FALSE, &renderTextureDSVDecriptorHandle);

	// 渲染到纹理时使用ImGui中视口的大小
	commandListDirectPre->RSSetViewports(1, &imGuiViewport);
	commandListDirectPre->RSSetScissorRects(1, &imGuiScissorRect);

	commandListDirectPre->ClearRenderTargetView(renderTextureRTVDescriptorHandle, (float*)&clearColor, 0, nullptr);
	commandListDirectPre->ClearDepthStencilView(renderTextureDSVDecriptorHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	// 这里也要设置一次描述符堆，和bundle录制时的设置要严格匹配
	ID3D12DescriptorHeap* skyboxDescriptorHeaps[] = { skyboxDescriptorHeap.Get(), skyboxSamplerDescriptorHeap.Get() };
	commandListDirectPre->SetDescriptorHeaps(_countof(skyboxDescriptorHeaps), skyboxDescriptorHeaps);
	commandListDirectPre->ExecuteBundle(skyboxBundle.Get());

	// 这里也要设置一次描述符堆，和bundle录制时的设置要严格匹配
	ID3D12DescriptorHeap* descriptorHeaps[] = { shaderResourceViewDescriptorHeap.Get(), samplerDescriptorHeap.Get() };
	commandListDirectPre->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
	commandListDirectPre->ExecuteBundle(sceneBundle.Get());

	//渲染完毕将渲染目标纹理由渲染目标状态转换回纹理状态，并准备显示
	commandListDirectPre->ResourceBarrier(1
		, &CD3DX12_RESOURCE_BARRIER::Transition(
			renderTexture.Get()
			,D3D12_RESOURCE_STATE_RENDER_TARGET
			, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | 
					    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));

	//关闭命令列表，去执行
	DXCheck(commandListDirectPre->Close(), L"commandListDirectPre close failed!");

	commandlists.clear();
	commandlists.emplace_back(commandListDirectPre.Get());

	commandQueue->ExecuteCommandLists(static_cast<uint32_t>(commandlists.size()), commandlists.data());

	waitCommandListComplete();

	DXCheck(commandAllocatorDirectPre->Reset(), L"commandAllocatorDirectPre reset failed!");
	DXCheck(commandListDirectPre->Reset(commandAllocatorDirectPre.Get(), graphicsPipelineState.Get()), L"commandListDirectPre reset failed!");

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
	commandListDirectPre->ResourceBarrier(1,
		&CD3DX12_RESOURCE_BARRIER::Transition(renderTargets[currentFrameIndex].Get(),
			D3D12_RESOURCE_STATE_PRESENT,
			D3D12_RESOURCE_STATE_RENDER_TARGET));

	CD3DX12_CPU_DESCRIPTOR_HANDLE renderTargetViewDescriptorHandle(CPU_DESCRIPTOR_HEAP_START(renderTargetViewDescriptorHeap),
		currentFrameIndex, renderTargetViewDescriptorSize);

	CD3DX12_CPU_DESCRIPTOR_HANDLE depthStencilBufferViewHandle(CPU_DESCRIPTOR_HEAP_START(depthStencilViewDescriptorHeap));

	commandListDirectPre->OMSetRenderTargets(1, &renderTargetViewDescriptorHandle, FALSE, &depthStencilBufferViewHandle);

	commandListDirectPre->ClearRenderTargetView(renderTargetViewDescriptorHandle, (float*)&clearColor, 0, nullptr);
	commandListDirectPre->ClearDepthStencilView(depthStencilBufferViewHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	// 这里也要设置一次描述符堆，和bundle录制时的设置要严格匹配
	commandListDirectPre->SetDescriptorHeaps(_countof(skyboxDescriptorHeaps), skyboxDescriptorHeaps);
	commandListDirectPre->ExecuteBundle(skyboxBundle.Get());

	//// 这里也要设置一次描述符堆，和bundle录制时的设置要严格匹配
	//commandListDirectPre->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
	//commandListDirectPre->ExecuteBundle(sceneBundle.Get());

	commandListDirectPre->SetPipelineState(graphicsPipelineState.Get());

	// 渲染render texture到ImGui::Image的修改测试
	//ID3D12DescriptorHeap* renderTextureDescriptorHeaps[] = { renderTextureSRVDescriptorHeap.Get(), samplerDescriptorHeap.Get() };
	ID3D12DescriptorHeap* renderTextureDescriptorHeaps[] = { imGuiShaderResourceViewDescriptorHeap.Get(), samplerDescriptorHeap.Get() };
	commandListDirectPre->SetDescriptorHeaps(_countof(renderTextureDescriptorHeaps), renderTextureDescriptorHeaps);

	// 渲染render texture到ImGui::Image的修改测试
	//commandListDirectPre->SetGraphicsRootDescriptorTable(0, GPU_DESCRIPTOR_HEAP_START(renderTextureSRVDescriptorHeap));

	CD3DX12_GPU_DESCRIPTOR_HANDLE renderTextureDescriptorHandle(GPU_DESCRIPTOR_HEAP_START(imGuiShaderResourceViewDescriptorHeap), 8, SRVCBVUAVDescriptorSize);
	commandListDirectPre->SetGraphicsRootDescriptorTable(0, renderTextureDescriptorHandle);

	// 渲染render texture到ImGui::Image的修改测试
	//CD3DX12_GPU_DESCRIPTOR_HANDLE renderTextureCBVHandle(GPU_DESCRIPTOR_HEAP_START(renderTextureSRVDescriptorHeap), 1, SRVCBVUAVDescriptorSize);
	CD3DX12_GPU_DESCRIPTOR_HANDLE renderTextureCBVHandle(GPU_DESCRIPTOR_HEAP_START(imGuiShaderResourceViewDescriptorHeap), 9, SRVCBVUAVDescriptorSize);

	commandListDirectPre->SetGraphicsRootDescriptorTable(1, renderTextureCBVHandle);

	CD3DX12_GPU_DESCRIPTOR_HANDLE samplerDescriptorHandle(GPU_DESCRIPTOR_HEAP_START(samplerDescriptorHeap), currentSamplerNo, samplerDescriptorSize);

	commandListDirectPre->SetGraphicsRootDescriptorTable(2, samplerDescriptorHandle);

	// 注意我们使用的渲染手法是三角形列表，也就是通常的Mesh网格
	commandListDirectPre->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandListDirectPre->IASetVertexBuffers(0, 1, &vertexBufferView);
	commandListDirectPre->IASetIndexBuffer(&indexBufferView);

	commandListDirectPre->DrawIndexedInstanced(indexCount, 1, 0, 0, 0);

	ID3D12DescriptorHeap* imGuiDescriptorHeaps[] = { imGuiShaderResourceViewDescriptorHeap.Get() };
	commandListDirectPre->SetDescriptorHeaps(1, imGuiDescriptorHeaps);

	renderImGui(commandListDirectPre);

	commandListDirectPre->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(renderTargets[currentFrameIndex].Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	DXCheck(commandListDirectPre->Close(), L"ID3D12GraphicsCommandList::Close failed!");

	ID3D12CommandList* commandLists[]{ commandListDirectPre.Get() };

	commandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);

	DXCheck(swapChain->Present(1, 0), L"IDXGISwapChain::Present failed!");

	waitCommandListComplete();

	// 到这里说明一个命令队列完整的执行完了，在这里就代表我们的一帧已经渲染完了，接着准备执行下一帧渲染
	// 获取新的后缓冲序号，因为Present真正完成时后缓冲的序号就更新了
	currentFrameIndex = swapChain->GetCurrentBackBufferIndex();

	// 命令分配器先Reset一下
	DXCheck(commandAllocatorDirectPre->Reset(), L"ID3D12CommandAllocator::Reset failed!");

	// Reset命令列表，并重新指定命令分配器和PSO对象
	DXCheck(commandListDirectPre->Reset(commandAllocatorDirectPre.Get(), graphicsPipelineState.Get()), L"ID3D12GraphicsCommandList::Reset failed!");
}

void D3DApp::waitCommandListComplete()
{
	//开始同步GPU与CPU的执行，先记录围栏标记值
	const UINT64 targetFenceValue = fenceValue;

	DXCheck(commandQueue->Signal(fence.Get(), targetFenceValue), L"ID3D12CommandQueue::Signal failed!");
	fenceValue++;

	// 看命令有没有真正执行到围栏标记的这里，没有就利用事件去等待，注意使用的是命令队列对象的指针
	if (fence->GetCompletedValue() < targetFenceValue)
	{
		DXCheck(fence->SetEventOnCompletion(targetFenceValue, fenceEvent), L"ID3D12Fence::SetEventOnCompletion failed!");
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
		app->onResize();
	}
	// Restore from minimized
	else if (app->restore)
	{
		if (app->minimized)
		{
			app->appPaused = false;
			app->minimized = false;
			app->onResize();
		}
		// Restoring from maximized state?
		else if (app->maximized)
		{
			app->appPaused = false;
			app->maximized = false;
			app->onResize();
		}
		else if (app->resizing)
		{

		}
		else
		{
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

	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
	{
		glfwSetWindowShouldClose(inWindow, true);
	}
}

void D3DApp::onResize()
{
	assert(device);
	assert(swapChain);
	assert(commandAllocatorDirectPre);

	// Flush before changing any resources.
	flushCommandQueue();

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
	DXCheck(commandListDirectPre->Close(), L"commandListDirectPre close failed!");
	ID3D12CommandList* cmdsLists[] = { commandListDirectPre.Get() };
	commandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

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

	DXCheck(commandListDirectPre->Reset(commandAllocatorDirectPre.Get(), nullptr), L"commandListDirectPre reset failed!");
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

