#pragma once

#define IMGUI_UNLIMITED_FRAME_RATE
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_dx12.h>
#include <backends/imgui_impl_win32.h>

#include <stdint.h>
#include <wrl.h>

using namespace Microsoft;
using namespace Microsoft::WRL;

struct ImGuiInitData
{
	struct GLFWwindow* window;
	HWND mainWindow;
	ComPtr<ID3D12Device> device;
	uint32_t frameBackbufferCount;
	ComPtr<struct ID3D12DescriptorHeap> shaderResourceViewDescriptorHeap;
};

class ImGuiLayer
{
public:
	void initialize(const ImGuiInitData& initData);
	void update();
	void render(const ComPtr<struct ID3D12GraphicsCommandList>& commandList);
private:

};