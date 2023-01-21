#pragma once

#include <windows.h>
#include <cstdio>

#include <intrin.h>

#define Assert(x) do { if (!(x)) __debugbreak(); } while (0)

static struct OutputDebugStringBuffer
{
	DWORD process_id;
	char  data[4096 - sizeof(DWORD)];
}*buffer;

static HANDLE outputDebugStringBufferDataReady;
static HANDLE outputDebugStringBufferReady;

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

	static DWORD WINAPI ods_proc(LPVOID arg)
	{
		DWORD ret = 0;

		HANDLE stderror = GetStdHandle(STD_ERROR_HANDLE);
		Assert(stderror);

		for (;;)
		{
			SetEvent(outputDebugStringBufferReady);

			DWORD wait = WaitForSingleObject(outputDebugStringBufferDataReady, INFINITE);
			Assert(wait == WAIT_OBJECT_0);

			DWORD length = 0;
			while (length < sizeof(buffer->data) && buffer->data[length] != 0)
			{
				length++;
			}

			if (length != 0)
			{
				DWORD written;
				WriteFile(stderror, buffer->data, length, &written, NULL);
			}
		}
	}

	void outputDebugStringCapture()
	{
		if (IsDebuggerPresent())
		{
			return;
		}

		HANDLE file = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(OutputDebugStringBuffer), "DBWIN_BUFFER");
		Assert(file != INVALID_HANDLE_VALUE);

		buffer = (OutputDebugStringBuffer*)MapViewOfFile(file, SECTION_MAP_READ, 0, 0, 0);
		Assert(buffer);

		outputDebugStringBufferReady = CreateEventA(NULL, FALSE, FALSE, "DBWIN_BUFFER_READY");
		Assert(outputDebugStringBufferReady);

		outputDebugStringBufferDataReady = CreateEventA(NULL, FALSE, FALSE, "DBWIN_DATA_READY");
		Assert(outputDebugStringBufferDataReady);

		HANDLE thread = CreateThread(NULL, 0, ods_proc, NULL, 0, NULL);
		Assert(thread);
	}
};