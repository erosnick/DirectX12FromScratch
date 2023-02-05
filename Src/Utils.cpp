#include "pch.h"
#include "Utils.h"
#include <comdef.h>
#include <string>
#include <fstream>
#include <sstream>
#include <d3dcompiler.h>
#include <vector>

ComPtr<IDxcUtils> dxcUtils;
ComPtr<IDxcCompiler3> dxcCompiler;

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

void createDXCCompiler()
{
	DXCheck(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils)), L"DxcCreateInstance failed!");
	DXCheck(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler)), L"DxcCreateInstance failed!");
}

void compileShader(const std::wstring& path, const std::wstring& entryPoint, const std::wstring& targetProfile, ComPtr<IDxcBlob>& shader)
{
	uint32_t codePage = CP_UTF8;
	uint32_t sourceSize = 0;

	std::ifstream shaderFile(path);

	std::stringstream shaderStream;

	shaderStream << shaderFile.rdbuf();

	shaderFile.close();

	auto shaderSource = shaderStream.str();

	ComPtr<IDxcBlobEncoding> shaderSourceBlob;
	DXCheck(dxcUtils->CreateBlob(shaderSource.data(), static_cast<uint32_t>(shaderSource.size()), codePage, &shaderSourceBlob), L"CreateBlobFromFile failed!");

	DxcBuffer shaderSourceBuffer;
	shaderSourceBuffer.Ptr = shaderSourceBlob->GetBufferPointer();
	shaderSourceBuffer.Size = shaderSourceBlob->GetBufferSize();
	shaderSourceBuffer.Encoding = 0;

	ComPtr<IDxcResult> compileResult;

	std::vector<LPCWSTR> arguments;

	// -E for the entry point(eg. PSMain)
	arguments.push_back(L"-E");
	arguments.push_back(entryPoint.c_str());

	// -T for the target profile(eg. ps_6_2)
	arguments.push_back(L"-T");
	arguments.push_back(targetProfile.c_str());

	// Strip reflection data and pdbs(see later)
	arguments.push_back(L"-Qstrip_debug");
	arguments.push_back(L"-Qstrip_reflect");

	arguments.push_back(DXC_ARG_WARNINGS_ARE_ERRORS);		// -WX
	arguments.push_back(DXC_ARG_DEBUG);					// -Zi
	//arguments.push_back(DXC_ARG_PACK_MATRIX_ROW_MAJOR);	// -zP

	auto hr = dxcCompiler->Compile(
		&shaderSourceBuffer,							// pSource
		arguments.data(),							// pArguments
		static_cast<uint32_t>(arguments.size()),	// argCount
		nullptr, IID_PPV_ARGS(&compileResult));

	// Error handling
	ComPtr<IDxcBlobUtf8> errors;
	ComPtr<IDxcBlobWide> outputName;
	compileResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), &outputName);

	if (errors && errors->GetStringLength() > 0)
	{
		wprintf(L"Compilation failed with errors:\n%hs\n", static_cast<const char*>(errors->GetBufferPointer()));
		DXThrow(hr, L"");
	}

	compileResult->GetResult(&shader);
}

ID3DBlob* loadShaderBinary(const std::string& path)
{
	std::ifstream shaderBinary(path, std::ios::binary);

	shaderBinary.seekg(0, std::ios_base::end);

	auto size = shaderBinary.tellg();
	shaderBinary.seekg(0, std::ios_base::beg);

	ID3DBlob* blob;
	DXCheck(D3DCreateBlob(size, &blob), L"D3DCreateBlob failed!");

	shaderBinary.read(static_cast<char*>(blob->GetBufferPointer()), size);
	shaderBinary.close();

	return blob;
}
