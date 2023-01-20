#pragma once

#include <stdexcept>
#include <windows.h>

#define DXThrow(message) throw std::runtime_error(message);

#define DXCheck(result, message) if (FAILED(result)) { DXThrow(message); }

class DxException
{
public:
	DxException() = default;
	DxException(HRESULT hr, const std::wstring& functionName, const std::wstring& filename, int lineNumber);

	std::wstring ToString() const;

	HRESULT ErrorCode = S_OK;
	std::wstring FunctionName;
	std::wstring Filename;
	int LineNumber = -1;
};
