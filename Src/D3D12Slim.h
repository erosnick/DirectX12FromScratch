#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <cstdint>
#include <array>
#include <cassert>

namespace D3D12Lite
{
	constexpr uint32_t NUM_FRAMES_IN_FLIGHT = 2;
	constexpr uint32_t NUM_BACK_BUFFERS = 3;
	constexpr uint32_t NUM_RTV_STAGING_DESCRIPTORS = 256;
	constexpr uint32_t NUM_DSV_STAGING_DESCRIPTORS = 32;
	constexpr uint32_t NUM_SRV_STAGING_DESCRIPTORS = 4096;
	constexpr uint32_t NUM_SAMPLER_DESCRIPTORS = 6;
	constexpr uint32_t MAX_QUEUED_BARRIERS = 16;
	constexpr uint8_t PER_OBJECT_SPACE = 0;
	constexpr uint8_t PER_MATERIAL_SPACE = 1;
	constexpr uint8_t PER_PASS_SPACE = 2;
	constexpr uint8_t PER_FRAME_SPACE = 3;
	constexpr uint8_t NUM_RESOURCE_SPACES = 4;
	constexpr uint32_t NUM_RESERVED_SRV_DESCRIPTORS = 8192;
	constexpr uint32_t IMGUI_RESERVED_DESCRIPTOR_INDEX = 0;
	constexpr uint32_t NUM_SRV_RENDER_PASS_USER_DESCRIPTORS = 65536;
	constexpr uint32_t INVALID_RESOURCE_TABLE_INDEX = UINT_MAX;
	constexpr uint32_t MAX_TEXTURE_SUBRESOURCE_COUNT = 32;
	static const wchar_t* SHADER_SOURCE_PATH = L"Shaders/";
	static const wchar_t* SHADER_OUTPUT_PATH = L"Shaders/Compiled/";
	static const char* RESOURCE_PATH = "Resources/";

	using SubResourceLayouts = std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, MAX_TEXTURE_SUBRESOURCE_COUNT>;

	enum class GPUResourceType : bool
	{
		Buffer = false,
		Texture = true
	};

	enum class BufferAccessFlags : uint8_t
	{
		GPUOnly = 0,
		HostWritable = 1
	};

	enum class BufferViewFlags : uint8_t
	{
		None = 0,
		CVB = 1,
		SRV = 2,
		UAV = 4
	};

	enum class TextureViewFlags : uint8_t
	{
		None = 0,
		RTV = 1,
		DSV = 2,
		SRV = 4,
		UAV = 8
	};

	enum class ContextWaitType : uint8_t
	{
		Host = 0,
		Graphics,
		Compute,
		Copy
	};

	enum class PipelineType : uint8_t
	{
		Graphics = 0,
		Compute
	};

	enum class ShaderType : uint8_t
	{
		Vertex = 0,
		Pixel,
		Compute
	};

	inline BufferAccessFlags operator|(BufferAccessFlags a, BufferAccessFlags b)
	{
		return static_cast<BufferAccessFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
	}

	inline BufferAccessFlags operator&(BufferAccessFlags a, BufferAccessFlags b)
	{
		return static_cast<BufferAccessFlags>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
	}

	inline BufferViewFlags operator|(BufferViewFlags a, BufferViewFlags b)
	{
		return static_cast<BufferViewFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
	}

	inline BufferViewFlags operator&(BufferViewFlags a, BufferViewFlags b)
	{
		return static_cast<BufferViewFlags>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
	}

	inline TextureViewFlags operator|(TextureViewFlags a, TextureViewFlags b)
	{
		return static_cast<TextureViewFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
	}

	inline TextureViewFlags operator&(TextureViewFlags a, TextureViewFlags b)
	{
		return static_cast<TextureViewFlags>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
	}

	struct BufferCreationDesc
	{
		uint32_t mSize = 0;
		uint32_t mStride = 0;
		BufferViewFlags mViewFlags = BufferViewFlags::None;
		BufferAccessFlags mAccessFlags = BufferAccessFlags::GPUOnly;
		bool mIsRawAccess = false;
	};

	struct TextureCreationDesc
	{
		TextureCreationDesc()
		{
			mResourceDesc.Format = DXGI_FORMAT_UNKNOWN;
			mResourceDesc.Width = 0;
			mResourceDesc.Height = 0;
			mResourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
			mResourceDesc.DepthOrArraySize = 1;
			mResourceDesc.MipLevels = 1;
			mResourceDesc.SampleDesc.Count = 1;
			mResourceDesc.SampleDesc.Quality = 0;
			mResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			mResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
			mResourceDesc.Alignment = 0;
		}

		D3D12_RESOURCE_DESC mResourceDesc{};
		TextureViewFlags mViewFlags = TextureViewFlags::None;
	};

	struct Descriptor
	{
		bool IsValid() const { return mCPUHandle.ptr != 0; }
		bool IsReferencedByShader() const { return mGPUHandle.ptr != 0; }

		D3D12_CPU_DESCRIPTOR_HANDLE mCPUHandle{ 0 };
		D3D12_GPU_DESCRIPTOR_HANDLE mGPUHandle{ 0 };
		uint32_t mHeapIndex = 0;
	};

	struct Resource
	{
		GPUResourceType mType = GPUResourceType::Buffer;
		D3D12_RESOURCE_DESC mDesc{};
		ComPtr<ID3D12Resource> mResource{};
		D3D12_GPU_VIRTUAL_ADDRESS mVirtualAddress = 0;
		D3D12_RESOURCE_STATES mState = D3D12_RESOURCE_STATE_COMMON;
		bool mIsReady = false;
		uint32_t mDescriptorHeapIndex = INVALID_RESOURCE_TABLE_INDEX;
	};

	struct BufferResource : public Resource
	{
		BufferResource() : Resource()
		{
			mType = GPUResourceType::Buffer;
		}

		void SetMappedData(void* data, size_t dataSize)
		{
			assert(mMappedResource != nullptr && data != nullptr && dataSize > 0 && dataSize <= mDesc.Width);
			memcpy_s(mMappedResource, mDesc.Width, data, dataSize);
		}

		uint8_t* mMappedResource = nullptr;
		uint32_t mStride = 0;
		Descriptor mCBVDescriptor{};
		Descriptor mSRVDescriptor{};
		Descriptor mUAVDescriptor{};
	};

	struct TextureResource : public Resource
	{
		TextureResource() : Resource()
		{
			mType = GPUResourceType::Texture;
		}

		Descriptor mRTVDescriptor{};
		Descriptor mDSVDescriptor{};
		Descriptor mSRVDescriptor{};
		Descriptor mUAVDescriptor{};
	};
}