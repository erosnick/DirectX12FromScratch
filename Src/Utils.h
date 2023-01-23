#pragma once

#include <stdexcept>
#include <windows.h>
#include <d3d12.h>
#include <string>

// 用于向上取整除法
#define UPPER_DIV(A, B) (static_cast<uint32_t>(((A) + ((B) - 1)) / (B)))

// 更简洁的向上取整算法
#define UPPER(A, B) (static_cast<uint32_t>(((A) + ((B) - 1)) &~ (B - 1)))

#define WIDE(x) L##x
#define WIDE1(x) WIDE(x)
#define FILE_NAME WIDE1(__FILE__)
#define FUNCTION_NAME WIDE1(__FUNCTION__)

inline void setD3D12DebugName(ID3D12Object * object, const std::wstring& name)
{
	object->SetName(name.c_str());
}

inline void setD3D12DebugNameIndexd(ID3D12Object* object, const std::wstring& name, uint32_t index)
{
	auto fullName = name + L"[" + std::to_wstring(index) + L"]";
	object->SetName(fullName.c_str());
}

class DxException
{
public:
	DxException() = default;
	DxException(HRESULT hr, const std::wstring& inFunctionName, const std::wstring& inFilename, int inLineNumber, const std::wstring& inErrorMessage = L"");

	std::wstring ToString() const;

	HRESULT errorCode = S_OK;
	std::wstring functionName;
	std::wstring filename;
	int lineNumber = -1;
	std::wstring errorMessage;
};

#define DXThrow(result, message) throw DxException(result, FUNCTION_NAME, FILE_NAME, __LINE__, message);

#define DXCheck(result, message) if (FAILED(result)) { DXThrow(result, message); }
