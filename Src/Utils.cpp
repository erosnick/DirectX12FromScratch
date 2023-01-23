#include "pch.h"
#include "Utils.h"
#include <comdef.h>
#include <string>

DxException::DxException(HRESULT hr, const std::wstring& inFunctionName, const std::wstring& inFilename, int inLineNumber, const std::wstring& inErrorMessage) :
	errorCode(hr),
	functionName(inFunctionName),
	filename(inFilename),
	lineNumber(inLineNumber),
	errorMessage(inErrorMessage)
{
}

std::wstring DxException::ToString()const
{
	// Get the string description of the error code.
	_com_error err(errorCode);
	std::wstring msg = err.ErrorMessage();

	return functionName + L" failed in " + filename + L"; line " + std::to_wstring(lineNumber) + L"; error: " + msg + L"[" + errorMessage + L"]";
}