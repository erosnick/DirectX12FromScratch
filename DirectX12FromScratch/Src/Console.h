#pragma once

#include <windows.h>
#include <cstdio>

class Console
{
public:
	Console()
	{
		FILE* stream = nullptr;
		AllocConsole();

		freopen_s(&stream, "CON", "r", stdin);
		freopen_s(&stream, "CON", "w", stdout);
		freopen_s(&stream, "CON", "w", stderr);
	}

	~Console()
	{
		FreeConsole();
	}
};