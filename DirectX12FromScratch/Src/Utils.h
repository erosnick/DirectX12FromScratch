#pragma once

#include <stdexcept>
#include <windows.h>

#define DXThrow(message) throw std::runtime_error(message);

#define DXCheck(result, message) if (FAILED(result)) { DXThrow(message); }

// ��������ȡ������
#define UPPER_DIV(A, B) (static_cast<uint32_t>(((A) + ((B) - 1)) / (B)))

// ����������ȡ���㷨
#define UPPER(A, B) (static_cast<uint32_t>(((A) + ((B) - 1)) &~ (B - 1)))

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
