// DirectX12.cpp : Defines the entry point for the application.
//

#include "pch.h"
#include "Console.h"
#include "7.D3DAppGLFW.h"
#include "Utils.h"

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR    lpCmdLine,
	_In_ int       nCmdShow)
{
	auto result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	Console console;
	console.outputDebugStringCapture();

	try
	{
		D3DApp app;
		if (!app.initialize(hInstance, nCmdShow))
			return 0;

		return app.run();
	}
	catch (DXException& e)
	{
		MessageBox(nullptr, e.ToWString().c_str(), L"HR Failed", MB_OK);
		return 0;
	}

	CoUninitialize();
}