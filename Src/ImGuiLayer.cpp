#include "pch.h"
#include "ImGuiLayer.h"

#include <d3d12.h>

#include "imgui_console.h"

csys::ItemLog& operator<<(csys::ItemLog& log, ImVec4& vec)
{
	log << "ImVec4: [" << vec.x << ", "
		<< vec.y << ", "
		<< vec.z << ", "
		<< vec.w << "]";
	return log;
}

static void imvec4_setter(ImVec4& my_type, std::vector<int> vec)
{
	if (vec.size() < 4) return;

	my_type.x = vec[0] / 255.f;
	my_type.y = vec[1] / 255.f;
	my_type.z = vec[2] / 255.f;
	my_type.w = vec[3] / 255.f;
}

void ImGuiLayer::initialize(const ImGuiInitData& initData)
{
	ImGui_ImplWin32_EnableDpiAwareness();

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;

	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;       // Enable Keyboard Controls
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;		// Enable Gamepad Controls
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;           // Enable Docking

	// DirectX 12下开启这个标志拖动ImGui窗口到视口外会崩溃，暂时先屏蔽(Vulkan下没这个问题)
	// 目前这个问题得到了官方的确认(2023.2.4)，待跟进
	//io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;         // Enable Multi-Viewport / Platform Windows

	// When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
	ImGuiStyle& style = ImGui::GetStyle();
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		style.WindowRounding = 0.0f;
		style.Colors[ImGuiCol_WindowBg].w = 1.0f;
	}

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
	//ImGui::StyleColorsLight();

	// Setup Platform/Renderer backends
	ImGui_ImplGlfw_InitForOther(initData.window, false);
	ImGui_ImplDX12_Init(initData.device.Get(), initData.frameBackbufferCount,
		DXGI_FORMAT_R8G8B8A8_UNORM, initData.shaderResourceViewDescriptorHeap.Get(),
		initData.shaderResourceViewDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		initData.shaderResourceViewDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

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
	io.Fonts->AddFontFromFileTTF("Assets/fonts/Roboto-Medium.ttf", 24.0f);
	//io.Fonts->AddFontFromFileTTF("Assets/fonts/ProggyClean.ttf", 20.0f);
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
	//ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, NULL, io.Fonts->GetGlyphRangesJapanese());
	//IM_ASSERT(font != NULL);

	initializeImGuiConsole();
}

void ImGuiLayer::initializeImGuiConsole()
{
	// Our state
	ImVec4 clear_color = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);

	///////////////////////////////////////////////////////////////////////////
	// IMGUI CONSOLE //////////////////////////////////////////////////////////
	///////////////////////////////////////////////////////////////////////////
	console = std::make_shared<ImGuiConsole>();

	// Register variables
	console->System().RegisterVariable("background_color", clear_color, imvec4_setter);

	// Register scripts
	console->System().RegisterScript("test_script", "./Assets/Scripts/console.script");

	// Register custom commands
	console->System().RegisterCommand("random_background_color", "Assigns a random color to the background application",
	[&clear_color]()
	{
		clear_color.x = (rand() % 256) / 256.f;
		clear_color.y = (rand() % 256) / 256.f;
		clear_color.z = (rand() % 256) / 256.f;
		clear_color.w = (rand() % 256) / 256.f;
	});
	console->System().RegisterCommand("reset_background_color", "Reset background color to its original value",
	[&clear_color, val = clear_color]()
	{
		clear_color = val;
	});

	// Log example information:
	console->System().Log(csys::ItemType::INFO) << "Welcome to the imgui-console example!" << csys::endl;
	console->System().Log(csys::ItemType::INFO) << "The following variables have been exposed to the console:" << csys::endl << csys::endl;
	console->System().Log(csys::ItemType::INFO) << "\tbackground_color - set: [int int int int]" << csys::endl;
	console->System().Log(csys::ItemType::INFO) << csys::endl << "Try running the following command:" << csys::endl;
	console->System().Log(csys::ItemType::INFO) << "\tset background_color [255 0 0 255]" << csys::endl << csys::endl;

	///////////////////////////////////////////////////////////////////////////
}

void ImGuiLayer::update()
{
	// Start the Dear ImGui frame
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
}

void ImGuiLayer::render(const ComPtr<struct ID3D12GraphicsCommandList>& commandList)
{
	// ImGui Console
	console->Draw();

	// Rendering
	ImGui::Render();

	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList.Get());

	ImGuiIO& io = ImGui::GetIO();

	// Update and Render additional Platform Windows
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		ImGui::UpdatePlatformWindows();
		ImGui::RenderPlatformWindowsDefault();
	}
}

void ImGuiLayer::dockSpaceBegin()
{
	static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

	// We are using the ImGuiWindowFlags_NoDocking flag to make the parent window not dockable into,
	// because it would be confusing to have two docking targets within each others.
	ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking;

	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->WorkPos);
	ImGui::SetNextWindowSize(viewport->WorkSize);
	ImGui::SetNextWindowViewport(viewport->ID);
	//ImGui::SetNextWindowBgAlpha(0.1f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
	window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

	// When using ImGuiDockNodeFlags_PassthruCentralNode, DockSpace() will render our background
	// and handle the pass-thru hole, so we ask Begin() to not render a background.
	if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
		window_flags |= ImGuiWindowFlags_NoBackground;

	// Important: note that we proceed even if Begin() returns false (aka window is collapsed).
	// This is because we want to keep our DockSpace() active. If a DockSpace() is inactive,
	// all active windows docked into it will lose their parent and become undocked.
	// We cannot preserve the docking relationship between an active window and an inactive docking, otherwise
	// any change of dockspace/settings would lead to windows being stuck in limbo and never being visible.
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin("DockSpace Demo", nullptr, window_flags);
	ImGui::PopStyleVar();

	ImGui::PopStyleVar(2);

	// Submit the DockSpace
	ImGuiIO& io = ImGui::GetIO();
	if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
	{
		ImGuiID dockspace_id = ImGui::GetID("DirectX12AppDockspace");
		ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
	}
}

void ImGuiLayer::docSpaceEnd()
{
	ImGui::End();
}
