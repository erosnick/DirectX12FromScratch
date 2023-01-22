// DirectX12.cpp : Defines the entry point for the application.
//

#include "pch.h"
#include "Console.h"
#include "5.D3DApp.h"
#include "Utils.h"

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR    lpCmdLine,
	_In_ int       nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	Console console;
	console.outputDebugStringCapture();

	try
	{
		D3DApp app;
		if (!app.initialize())
			return 0;

		return app.run();
	}
	catch (DxException& e)
	{
		MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
		return 0;
	}
}