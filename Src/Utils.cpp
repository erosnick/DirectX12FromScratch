#include "pch.h"
#include "Utils.h"
#include <comdef.h>
#include <string>
#include <fstream>
#include <sstream>
#include <d3dx12.h>
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

ComPtr<ID3D12Resource> Utils::createDefaultBuffer(const ComPtr<ID3D12Device>& device, const ComPtr<ID3D12GraphicsCommandList>& commandList, const void* initData, UINT64 byteSize, ComPtr<ID3D12Resource>& uploadBuffer)
{
	ComPtr<ID3D12Resource> defaultBuffer;

	// Create the actual default buffer resource.
	DXCheck(device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(byteSize),
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(defaultBuffer.GetAddressOf())), L"Utils::createDefaultBuffer failed");

	// In order to copy CPU memory data into our default buffer, we need to create
	// an intermediate upload heap. 
	DXCheck(device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(byteSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(uploadBuffer.GetAddressOf())), L"Utils::createDefaultBuffer failed");

	// Describe the data we want to copy into the default buffer.
	D3D12_SUBRESOURCE_DATA subResourceData = {};
	subResourceData.pData = initData;
	subResourceData.RowPitch = byteSize;
	subResourceData.SlicePitch = subResourceData.RowPitch;

	// Schedule to copy the data to the default buffer resource.  At a high level, the helper function UpdateSubresources
	// will copy the CPU memory into the intermediate upload heap.  Then, using ID3D12CommandList::CopySubresourceRegion,
	// the intermediate upload heap data will be copied to mBuffer.
	commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer.Get(),
		D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST));
	UpdateSubresources<1>(commandList.Get(), defaultBuffer.Get(), uploadBuffer.Get(), 0, 0, 1, &subResourceData);
	commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ));

	// Note: uploadBuffer has to be kept alive after the above function calls because
	// the command list has not been executed yet that performs the actual copy.
	// The caller can Release the uploadBuffer after it knows the copy has been executed.
	return defaultBuffer;
}

void Utils::createDXCCompiler()
{
	DXCheck(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils)), L"DxcCreateInstance failed!");
	DXCheck(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler)), L"DxcCreateInstance failed!");
}

void Utils::compileShader(const std::wstring& path, const std::wstring& entryPoint, const std::wstring& targetProfile, ComPtr<IDxcBlob>& shader)
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

#if defined(_DEBUG)
	arguments.push_back(DXC_ARG_DEBUG);					// -Zi
#endif
	//arguments.push_back(DXC_ARG_PACK_MATRIX_ROW_MAJOR);		// -zP

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
		TCHAR buffer[256];
		wsprintf(buffer, L"Compilation failed with errors:\n%hs\n", static_cast<const char*>(errors->GetBufferPointer()));
		DXThrow(hr, buffer);
	}

	// Compilation success, get the shader data
	compileResult->GetResult(&shader);
}

ID3DBlob* Utils::loadShaderBinary(const std::string& path)
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
